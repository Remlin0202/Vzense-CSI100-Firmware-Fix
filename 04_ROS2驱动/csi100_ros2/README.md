# csi100_driver — Vzense CSI100 ROS 2 driver

ROS 2 (Jazzy) driver for the Vzense CSI100 ToF camera. Publishes depth, IR,
camera intrinsics read from the device, and an XYZ point cloud.

## Topics (node name `csi100_driver`)

| Topic | Type | Notes |
|---|---|---|
| `/csi100_driver/depth/image_raw` | `sensor_msgs/Image` | `16UC1`, **millimetres**, `0xFFFF` = invalid |
| `/csi100_driver/ir/image_raw` | `sensor_msgs/Image` | `16UC1`, IR intensity |
| `/csi100_driver/depth/camera_info` | `sensor_msgs/CameraInfo` | intrinsics from `Ps2_GetCameraParameters` |
| `/csi100_driver/ir/camera_info` | `sensor_msgs/CameraInfo` | IR is registered to depth |
| `/csi100_driver/points` | `sensor_msgs/PointCloud2` | xyz in metres, optical frame, stride subsampled |

All use `SensorDataQoS` (best effort). Frame id defaults to
`csi100_depth_optical_frame`.

## Parameters (`config/csi100_params.yaml`)

| Param | Default | Meaning |
|---|---|---|
| `frame_id` | `csi100_depth_optical_frame` | frame for all messages |
| `data_mode` | `2` | `PsDataMode`: 2 = `PsDepthAndIR_30` |
| `depth_range` | `1` | 0 = near, 1 = mid, 2 = far (mid ≈ 0.28–2.8 m) |
| `publish_pointcloud` | `true` | |
| `pointcloud_stride` | `2` | subsample factor for the cloud |
| `max_range_mm` | `0` | clamp beyond this to invalid (0 = keep raw) |
| `reconnect_timeout` | `5.0` | re-init the camera after this many seconds with no frame |
| `device_index` | `0` | which Vzense device to open |

## Build & run

```bash
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
# SDK location (bundled in the share package). Pick one:
#   export CSI100_SDK_ROOT="$(pwd)/03_SDK与工具/VzenseSDK_Linux"   # from the package root
#   ln -s ../../03_SDK与工具/VzenseSDK_Linux sdk                   # inside this package dir
colcon build --packages-select csi100_driver
source install/setup.bash
ros2 run csi100_driver csi100_driver_node            # defaults
ros2 launch csi100_driver csi100_driver.launch.py    # with the yaml params
```

Check it:

```bash
ros2 topic list | grep csi100
ros2 topic hz /csi100_driver/depth/image_raw
```

## Things that will bite you

- **12 V rail is mandatory.** USB 5 V alone enumerates the device but no
  frames ever arrive; the camera reports `12V adapter is disconnected`.
  Without it `SetDataMode` also returns `-105`.
- **USB bandwidth and the frame rate you actually get.** The camera is a USB
  2.0 High-Speed device (480 Mbps is its own ceiling; a USB 3.0 port does
  **not** help). Measured on the Q6A:

  | `data_mode` | meaning | depth | IR |
  |---|---|---|---|
  | `2` | `PsDepthAndIR_30` — depth + IR | 22.1 Hz | 21.9 Hz |
  | `0` | `PsDepthAndRGB_30` — no RGB sensor, so effectively **depth only** | **29.7 Hz** | none |
  | `11` | `PsWDR_Depth` | unsupported, `SetDataMode` = -105 | — |

  > Note: WDR (mode 11) was measured later to actually produce frames on some
  > firmware/power combinations (Windows FrameViewer once read 4850 mm, beyond
  > the mid-range nominal 2.8 m). The table above is from early Q6A tests and
  > is pending re-verification.

  depth+IR is 1.23 MB per frame; 30 fps would need ~295 Mbps, which is at the
  practical ceiling of High-Speed bulk. Use `data_mode:=0` for full-rate depth
  (road scanning), `2` when you also want the IR image.
- **No hardware trigger.** The 3-pin connector carries power only (two
  contacts), so there is no TRIG/SYNC; stamps are ROS time at receive.
- **Auto recovery.** If frames stop (USB re-enumeration, power glitch) the
  node closes and re-opens the device after `reconnect_timeout` seconds.
- **SDK is bundled in the share package** at `03_SDK与工具/VzenseSDK_Linux`.
  Point `CSI100_SDK_ROOT` there, or symlink it as `sdk` inside this package
  (see Build &amp; run).
