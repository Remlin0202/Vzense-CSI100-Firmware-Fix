// CSI100 ToF camera driver for ROS 2 (Jazzy).
//
// Publishes:
//   ~/depth/image_raw    sensor_msgs/Image   16UC1, millimetres, 0xFFFF = invalid
//   ~/ir/image_raw       sensor_msgs/Image   16UC1, IR intensity
//   ~/depth/camera_info  sensor_msgs/CameraInfo  (intrinsics read from the camera)
//   ~/ir/camera_info     sensor_msgs/CameraInfo  (IR is registered to depth)
//   ~/points             sensor_msgs/PointCloud2 (xyz in metres, optical frame)
//
// Notes:
//   * The camera MUST be powered from its 12 V rail as well as USB, otherwise no
//     frames arrive (the camera reports "12V adapter is disconnected").
//   * The driver re-initialises itself if frames stop for `reconnect_timeout`
//     seconds, which is what happens when the USB device re-enumerates.
//   * Frames carry no host-synchronised hardware trigger (the 3-pin connector is
//     power only), so stamps are the node's ROS time at receive.

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

#include "Vzense_api2.h"

static const uint16_t INVALID_DEPTH = 0xFFFF;

class Csi100Driver : public rclcpp::Node
{
public:
  explicit Csi100Driver(const rclcpp::NodeOptions & opts)
  : Node("csi100_driver", opts)
  {
    frame_id_        = declare_parameter<std::string>("frame_id", "csi100_depth_optical_frame");
    depth_range_     = declare_parameter<int>("depth_range", 1);        // 1 = PsMidRange
    // 0 = PsDepthAndRGB_30. The CSI100 has no RGB sensor, so this really means
    // "depth only" -> the full 30 Hz on USB 2.0 High-Speed.
    // Use 2 (PsDepthAndIR_30) when the IR image is wanted: both streams then
    // share the bandwidth and each runs at ~22 Hz.
    data_mode_       = declare_parameter<int>("data_mode", 0);
    publish_cloud_   = declare_parameter<bool>("publish_pointcloud", true);
    cloud_stride_    = declare_parameter<int>("pointcloud_stride", 2);
    max_range_mm_    = declare_parameter<int>("max_range_mm", 0);       // 0 = off
    reconnect_to_    = declare_parameter<double>("reconnect_timeout", 5.0);
    device_index_    = declare_parameter<int>("device_index", 0);

    if (cloud_stride_ < 1) cloud_stride_ = 1;

    auto qos = rclcpp::SensorDataQoS();

    pub_depth_ = create_publisher<sensor_msgs::msg::Image>("~/depth/image_raw", qos);
    pub_ir_    = create_publisher<sensor_msgs::msg::Image>("~/ir/image_raw", qos);
    pub_dinfo_ = create_publisher<sensor_msgs::msg::CameraInfo>("~/depth/camera_info", qos);
    pub_iinfo_ = create_publisher<sensor_msgs::msg::CameraInfo>("~/ir/camera_info", qos);
    pub_cloud_ = create_publisher<sensor_msgs::msg::PointCloud2>("~/points", qos);

    RCLCPP_INFO(get_logger(), "CSI100 driver starting (frame=%s, range=%d, mode=%d)",
                frame_id_.c_str(), depth_range_, data_mode_);

    if (!open_device()) {
      RCLCPP_ERROR(get_logger(), "could not open the camera; will retry from the capture thread");
    }

    running_ = true;
    capture_thread_ = std::thread(&Csi100Driver::capture_loop, this);
  }

  ~Csi100Driver() override
  {
    running_ = false;
    if (capture_thread_.joinable()) capture_thread_.join();
    close_device();
    Ps2_Shutdown();
  }

private:
  // ------------------------------------------------------------------ camera
  bool open_device()
  {
    if (!sdk_ready_) {
      if (Ps2_Initialize() != PsRetOK) { RCLCPP_ERROR(get_logger(), "Ps2_Initialize failed"); return false; }
      sdk_ready_ = true;
    }

    uint32_t count = 0;
    for (int i = 0; i < 30 && rclcpp::ok(); i++) {
      if (Ps2_GetDeviceCount(&count) == PsRetOK && count > 0) break;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (count == 0) { RCLCPP_WARN(get_logger(), "no Vzense device found"); return false; }

    PsDeviceInfo info;
    std::memset(&info, 0, sizeof(info));
    if (Ps2_GetDeviceInfo(&info, device_index_) != PsRetOK) { RCLCPP_WARN(get_logger(), "GetDeviceInfo failed"); return false; }

    RCLCPP_INFO(get_logger(), "opening %s  alias=%s  type=%d  fw=%s",
                info.uri, info.alias, static_cast<int>(info.devicetype), info.fw);

    if (Ps2_OpenDevice(info.uri, &handle_) != PsRetOK) { RCLCPP_WARN(get_logger(), "OpenDevice failed"); return false; }

    session_ = 0;
    if (Ps2_StartStream(handle_, session_) != PsRetOK) {
      RCLCPP_WARN(get_logger(), "StartStream failed");
      Ps2_CloseDevice(&handle_); handle_ = nullptr; return false;
    }

    int mode = data_mode_;
    int dm = Ps2_SetDataMode(handle_, session_, static_cast<PsDataMode>(mode));
    if (dm != PsRetOK) RCLCPP_WARN(get_logger(), "SetDataMode(%d) = %d", mode, dm);

    int dr = Ps2_SetDepthRange(handle_, session_, static_cast<PsDepthRange>(depth_range_));
    if (dr != PsRetOK) RCLCPP_WARN(get_logger(), "SetDepthRange(%d) = %d", depth_range_, dr);

    // intrinsics (depth); the IR image is registered to depth on this model
    PsCameraParameters param;
    std::memset(&param, 0, sizeof(param));
    if (Ps2_GetCameraParameters(handle_, session_, PsDepthSensor, &param) == PsRetOK) {
      fx_ = param.fx; fy_ = param.fy; cx_ = param.cx; cy_ = param.cy;
      have_intrinsics_ = true;
      info_msg_ = build_camera_info();
      RCLCPP_INFO(get_logger(), "intrinsics: fx=%.2f fy=%.2f cx=%.2f cy=%.2f", fx_, fy_, cx_, cy_);
    } else {
      RCLCPP_WARN(get_logger(), "GetCameraParameters failed - camera_info will be zero");
    }

    last_frame_ = std::chrono::steady_clock::now();
    return true;
  }

  void close_device()
  {
    if (handle_) {
      Ps2_StopStream(handle_, session_);
      Ps2_CloseDevice(&handle_);
      handle_ = nullptr;
    }
  }

  sensor_msgs::msg::CameraInfo build_camera_info() const
  {
    sensor_msgs::msg::CameraInfo ci;
    ci.header.frame_id = frame_id_;
    ci.width  = 0;   // filled per frame
    ci.height = 0;
    ci.distortion_model = "rational_polynomial";
    ci.d.assign({0, 0, 0, 0, 0, 0, 0, 0});
    ci.k = {fx_, 0.0, cx_, 0.0, fy_, cy_, 0.0, 0.0, 1.0};
    ci.r = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    ci.p = {fx_, 0.0, cx_, 0.0, 0.0, fy_, cy_, 0.0, 0.0, 0.0, 1.0, 0.0};
    ci.binning_x = 0;
    ci.binning_y = 0;
    ci.roi.do_rectify = false;
    return ci;
  }

  // ----------------------------------------------------------------- capture
  void capture_loop()
  {
    while (running_ && rclcpp::ok()) {
      if (!handle_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!open_device()) continue;
      }

      PsFrameReady ready;
      std::memset(&ready, 0, sizeof(ready));
      if (Ps2_ReadNextFrame(handle_, session_, &ready) != PsRetOK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check_stall();
        continue;
      }

      bool got = false;

      if (ready.depth) {
        PsFrame f;
        std::memset(&f, 0, sizeof(f));
        if (Ps2_GetFrame(handle_, session_, PsDepthFrame, &f) == PsRetOK && f.pFrameData) {
          publish_image(pub_depth_, pub_dinfo_, f, true);
          got = true;
        }
      }
      if (ready.ir) {
        PsFrame f;
        std::memset(&f, 0, sizeof(f));
        if (Ps2_GetFrame(handle_, session_, PsIRFrame, &f) == PsRetOK && f.pFrameData) {
          publish_image(pub_ir_, pub_iinfo_, f, false);
          got = true;
        }
      }
      if (got) last_frame_ = std::chrono::steady_clock::now(); else check_stall();
    }
  }

  void check_stall()
  {
    if (!handle_) return;
    const double idle = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - last_frame_).count();
    if (idle < reconnect_to_) return;
    RCLCPP_WARN(get_logger(), "no frames for %.1f s - re-initialising the camera "
                              "(check the 12 V rail / USB enumeration)", idle);
    close_device();
    last_frame_ = std::chrono::steady_clock::now();
  }

  // ----------------------------------------------------------------- publish
  void publish_image(const rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr & pub,
                     const rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr & pub_info,
                     const PsFrame & f, bool is_depth)
  {
    const uint32_t w = f.width, h = f.height;
    auto msg = std::make_unique<sensor_msgs::msg::Image>();
    msg->header.stamp = now();
    msg->header.frame_id = frame_id_;
    msg->width = w;
    msg->height = h;
    msg->encoding = "16UC1";
    msg->is_bigendian = 0;
    msg->step = w * 2;
    msg->data.resize(static_cast<size_t>(w) * h * 2);
    std::memcpy(msg->data.data(), f.pFrameData, msg->data.size());

    if (is_depth && max_range_mm_ > 0) {
      uint16_t * p = reinterpret_cast<uint16_t *>(msg->data.data());
      const size_t n = static_cast<size_t>(w) * h;
      const uint16_t lim = static_cast<uint16_t>(max_range_mm_);
      for (size_t i = 0; i < n; i++) if (p[i] >= lim) p[i] = INVALID_DEPTH;
    }

    pub->publish(std::move(msg));

    if (have_intrinsics_) {
      auto ci = std::make_unique<sensor_msgs::msg::CameraInfo>(info_msg_);
      ci->header.stamp = now();
      ci->width = w;
      ci->height = h;
      pub_info->publish(std::move(ci));
    }

    if (is_depth && publish_cloud_ && have_intrinsics_) publish_cloud(f);
  }

  void publish_cloud(const PsFrame & f)
  {
    const uint16_t * d = reinterpret_cast<const uint16_t *>(f.pFrameData);
    const uint32_t w = f.width, h = f.height;

    // count valid (strided) points first
    size_t n = 0;
    for (uint32_t v = 0; v < h; v += cloud_stride_)
      for (uint32_t u = 0; u < w; u += cloud_stride_) {
        const uint16_t z = d[static_cast<size_t>(v) * w + u];
        if (z > 0 && z < INVALID_DEPTH) n++;
      }

    auto msg = std::make_unique<sensor_msgs::msg::PointCloud2>();
    msg->header.stamp = now();
    msg->header.frame_id = frame_id_;
    msg->height = 1;
    msg->width = n;
    msg->is_dense = false;
    msg->is_bigendian = false;
    msg->point_step = 12;                       // x,y,z as float32
    msg->row_step = msg->point_step * n;
    msg->fields.resize(3);
    const char * names[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
      msg->fields[i].name = names[i];
      msg->fields[i].offset = i * 4;
      msg->fields[i].datatype = sensor_msgs::msg::PointField::FLOAT32;
      msg->fields[i].count = 1;
    }
    msg->data.resize(msg->row_step);

    float * out = reinterpret_cast<float *>(msg->data.data());
    size_t k = 0;
    for (uint32_t v = 0; v < h; v += cloud_stride_)
      for (uint32_t u = 0; u < w; u += cloud_stride_) {
        const uint16_t zmm = d[static_cast<size_t>(v) * w + u];
        if (zmm == 0 || zmm >= INVALID_DEPTH) continue;
        const float z = zmm / 1000.0f;          // metres, optical axis
        out[k * 3 + 0] = static_cast<float>((u - cx_) / fx_) * z;
        out[k * 3 + 1] = static_cast<float>((v - cy_) / fy_) * z;
        out[k * 3 + 2] = z;
        k++;
      }
    pub_cloud_->publish(std::move(msg));
  }

  // ------------------------------------------------------------------ state
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_depth_, pub_ir_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr pub_dinfo_, pub_iinfo_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr pub_cloud_;

  std::string frame_id_;
  int depth_range_, data_mode_, cloud_stride_, max_range_mm_, device_index_;
  bool publish_cloud_;
  double reconnect_to_;

  bool sdk_ready_ = false;
  PsDeviceHandle handle_ = nullptr;
  uint32_t session_ = 0;

  bool have_intrinsics_ = false;
  double fx_ = 0, fy_ = 0, cx_ = 0, cy_ = 0;
  sensor_msgs::msg::CameraInfo info_msg_;

  std::atomic<bool> running_{false};
  std::thread capture_thread_;
  std::chrono::steady_clock::time_point last_frame_{std::chrono::steady_clock::now()};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Csi100Driver>(rclcpp::NodeOptions()));
  rclcpp::shutdown();
  return 0;
}
