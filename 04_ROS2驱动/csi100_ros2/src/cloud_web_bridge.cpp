// cloud_web_bridge - read-only companion to the csi100_driver node.
//
// It subscribes to the PointCloud2 topic published by the driver and serves the
// latest cloud over plain HTTP, so the point cloud can be inspected from any
// browser (e.g. http://<board-ip>:8766 ) without RViz and without a desktop
// session.  It never talks to the camera and never publishes anything.
//
// Parameters:
//   topic      (string, default /csi100_driver/points)
//   port       (int,    default 8766)
//   stride     (int,    default 2)  keep every Nth point
//   max_points (int,    default 120000)

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

const char* kHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>CSI100 point cloud</title>
<style>
  html,body{margin:0;height:100%;background:#0e1116;color:#dfe6f0;
            font:13px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;overflow:hidden}
  canvas{display:block;width:100vw;height:100vh;cursor:grab}
  #hud{position:fixed;left:12px;top:12px;padding:8px 12px;border-radius:8px;
       background:rgba(0,0,0,.55);border:1px solid #2a3444;white-space:pre;pointer-events:none}
  #hud b{color:#7fd1ff}
  #err{position:fixed;inset:0;display:none;place-items:center;text-align:center;color:#ff8a8a}
</style></head>
<body>
<canvas id="cv"></canvas>
<div id="hud">CSI100 point cloud</div>
<div id="err"></div>
<script>
const cv = document.getElementById('cv');
const hud = document.getElementById('hud');
const gl = cv.getContext('webgl', {antialias:false, preserveDrawingBuffer:false});
if (!gl) { document.getElementById('err').style.display='grid';
           document.getElementById('err').textContent='WebGL unavailable'; }

const VS = `
attribute vec3 aPos;
uniform mat4 uMVP;
uniform float uPointSize;
varying float vZ;
void main(){
  gl_Position = uMVP * vec4(aPos, 1.0);
  vZ = aPos.z;
  gl_PointSize = uPointSize;
}`;
const FS = `
precision mediump float;
varying float vZ;
uniform float uMin, uMax;
void main(){
  float t = clamp((vZ - uMin) / max(uMax - uMin, 1e-6), 0.0, 1.0);
  vec3 near = vec3(0.25, 0.95, 0.55);
  vec3 mid  = vec3(0.98, 0.82, 0.25);
  vec3 far  = vec3(0.95, 0.30, 0.25);
  vec3 c = t < 0.5 ? mix(near, mid, t*2.0) : mix(mid, far, (t-0.5)*2.0);
  gl_FragColor = vec4(c, 1.0);
}`;

function sh(type, src){
  const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) console.error(gl.getShaderInfoLog(s));
  return s;
}
const prog = gl.createProgram();
gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS));
gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
gl.linkProgram(prog); gl.useProgram(prog);
const aPos = gl.getAttribLocation(prog, 'aPos');
const uMVP = gl.getUniformLocation(prog, 'uMVP');
const uMin = gl.getUniformLocation(prog, 'uMin');
const uMax = gl.getUniformLocation(prog, 'uMax');
const uPS  = gl.getUniformLocation(prog, 'uPointSize');
const buf = gl.createBuffer();
gl.enableVertexAttribArray(aPos);
gl.bindBuffer(gl.ARRAY_BUFFER, buf);
gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);

// ---- math -----------------------------------------------------------------
function perspective(fovy, aspect, near, far){
  const f = 1.0 / Math.tan(fovy/2), nf = 1/(near-far);
  return new Float32Array([f/aspect,0,0,0, 0,f,0,0, 0,0,(far+near)*nf,-1, 0,0,2*far*near*nf,0]);
}
function lookAt(eye, ctr, up){
  let zx=eye[0]-ctr[0], zy=eye[1]-ctr[1], zz=eye[2]-ctr[2];
  let l=Math.hypot(zx,zy,zz)||1; zx/=l; zy/=l; zz/=l;
  let xx=up[1]*zz-up[2]*zy, xy=up[2]*zx-up[0]*zz, xz=up[0]*zy-up[1]*zx;
  l=Math.hypot(xx,xy,xz)||1; xx/=l; xy/=l; xz/=l;
  const yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
  return new Float32Array([xx,yx,zx,0, xy,yy,zy,0, xz,yz,zz,0,
    -(xx*eye[0]+xy*eye[1]+xz*eye[2]),
    -(yx*eye[0]+yy*eye[1]+yz*eye[2]),
    -(zx*eye[0]+zy*eye[1]+zz*eye[2]), 1]);
}
function mul(a,b){
  const o=new Float32Array(16);
  for(let i=0;i<4;i++) for(let j=0;j<4;j++){
    o[i*4+j]=a[0*4+j]*b[i*4+0]+a[1*4+j]*b[i*4+1]+a[2*4+j]*b[i*4+2]+a[3*4+j]*b[i*4+3];
  }
  return o;
}

// ---- camera ---------------------------------------------------------------
let yaw=0.45, pitch=0.35, dist=3.0;
let tgt=[0,0,1.2], fitted=false, zmin=0.1, zmax=3.0, npts=0, fps=0;
let drag=false, lx=0, ly=0;
cv.addEventListener('mousedown', e=>{drag=true; lx=e.clientX; ly=e.clientY;});
window.addEventListener('mouseup', ()=>drag=false);
window.addEventListener('mousemove', e=>{
  if(!drag) return;
  yaw   -= (e.clientX-lx)*0.006;
  pitch += (e.clientY-ly)*0.006;
  pitch = Math.max(-1.45, Math.min(1.45, pitch));
  lx=e.clientX; ly=e.clientY;
});
cv.addEventListener('wheel', e=>{
  e.preventDefault();
  dist = Math.max(0.15, Math.min(60.0, dist*Math.exp(e.deltaY*0.0012)));
}, {passive:false});
cv.addEventListener('dblclick', ()=>{fitted=false;});

function resize(){
  const d = Math.min(window.devicePixelRatio||1, 2);
  cv.width = Math.floor(cv.clientWidth*d);
  cv.height = Math.floor(cv.clientHeight*d);
  gl.viewport(0,0,cv.width,cv.height);
}
window.addEventListener('resize', resize); resize();

function fit(pts){
  let mnx=1e9,mny=1e9,mnz=1e9,mxx=-1e9,mxy=-1e9,mxz=-1e9;
  for(let i=0;i<pts.length;i+=3){
    if(pts[i]<mnx)mnx=pts[i]; if(pts[i]>mxx)mxx=pts[i];
    if(pts[i+1]<mny)mny=pts[i+1]; if(pts[i+1]>mxy)mxy=pts[i+1];
    if(pts[i+2]<mnz)mnz=pts[i+2]; if(pts[i+2]>mxz)mxz=pts[i+2];
  }
  tgt=[(mnx+mxx)/2,(mny+mxy)/2,(mnz+mxz)/2];
  const r=Math.hypot(mxx-mnx,mxy-mny,mxz-mnz)||1;
  dist=r*1.6; zmin=mnz; zmax=mxz; fitted=true;
}

function draw(){
  requestAnimationFrame(draw);
  if(!npts) return;
  const eye=[tgt[0]+dist*Math.cos(pitch)*Math.sin(yaw),
             tgt[1]+dist*Math.sin(pitch),
             tgt[2]+dist*Math.cos(pitch)*Math.cos(yaw)];
  const P=perspective(50*Math.PI/180, cv.width/Math.max(cv.height,1), 0.01, 500);
  const V=lookAt(eye, tgt, [0,-1,0]);
  gl.clearColor(0.055,0.067,0.086,1);
  gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
  gl.enable(gl.DEPTH_TEST);
  gl.uniformMatrix4fv(uMVP,false,mul(P,V));
  gl.uniform1f(uMin,zmin); gl.uniform1f(uMax,zmax);
  gl.uniform1f(uPS, Math.max(1.5, Math.min(6.0, 900/dist)));
  gl.drawArrays(gl.POINTS,0,npts);
}
draw();

// ---- data -----------------------------------------------------------------
let busy=false, last=performance.now(), acc=0, frames=0;
async function poll(){
  if(!busy){
    busy=true;
    try{
      const r = await fetch('/points?t='+Date.now(), {cache:'no-store'});
      const ab = await r.arrayBuffer();
      const pts = new Float32Array(ab);
      if(pts.length){
        gl.bindBuffer(gl.ARRAY_BUFFER, buf);
        gl.bufferData(gl.ARRAY_BUFFER, pts, gl.DYNAMIC_DRAW);
        gl.vertexAttribPointer(aPos,3,gl.FLOAT,false,0,0);
        npts = pts.length/3;
        if(!fitted) fit(pts);
        else { // keep the depth range fresh
          let mn=1e9,mx=-1e9;
          for(let i=2;i<pts.length;i+=3){ if(pts[i]<mn)mn=pts[i]; if(pts[i]>mx)mx=pts[i]; }
          zmin+=(mn-zmin)*0.2; zmax+=(mx-zmax)*0.2;
        }
      }
      const now=performance.now(); acc+=now-last; last=now; frames++;
      if(frames>=10){ fps=1000/(acc/frames); acc=0; frames=0; }
    }catch(e){}
    busy=false;
  }
  hud.innerHTML = '<b>CSI100</b>  points ' + npts.toLocaleString()
    + '   view ' + fps.toFixed(0) + ' fps\n'
    + 'drag = orbit   wheel = zoom   dblclick = refit\n'
    + 'depth ' + zmin.toFixed(2) + ' - ' + zmax.toFixed(2) + ' m';
  setTimeout(poll, 60);
}
poll();
</script></body></html>
)HTML";

}  // namespace

class CloudWebBridge : public rclcpp::Node
{
public:
  CloudWebBridge() : Node("cloud_web_bridge")
  {
    topic_      = declare_parameter<std::string>("topic", "/csi100_driver/points");
    port_       = declare_parameter<int>("port", 8766);
    stride_     = declare_parameter<int>("stride", 2);
    max_points_ = declare_parameter<int>("max_points", 120000);
    if (stride_ < 1) stride_ = 1;

    sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      topic_, rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::PointCloud2::SharedPtr msg) { onCloud(msg); });

    RCLCPP_INFO(get_logger(), "serving %s on http://0.0.0.0:%d", topic_.c_str(), port_);

    running_ = true;
    server_thread_ = std::thread(&CloudWebBridge::serverLoop, this);
  }

  ~CloudWebBridge() override
  {
    running_ = false;
    if (server_thread_.joinable()) server_thread_.join();
  }

private:
  void onCloud(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
  {
    int ox = -1, oy = -1, oz = -1;
    for (const auto& f : msg->fields) {
      if (f.name == "x") ox = static_cast<int>(f.offset);
      else if (f.name == "y") oy = static_cast<int>(f.offset);
      else if (f.name == "z") oz = static_cast<int>(f.offset);
    }
    if (ox < 0 || oy < 0 || oz < 0) {
      RCLCPP_WARN_ONCE(get_logger(), "cloud has no x/y/z float fields");
      return;
    }
    const uint32_t count = msg->width * msg->height;
    const uint32_t step  = msg->point_step;
    if (step == 0 || msg->data.size() < static_cast<size_t>(count) * step) return;

    std::vector<float> out;
    out.reserve(count / static_cast<uint32_t>(stride_) * 3 + 3);
    const uint8_t* base = msg->data.data();
    for (uint32_t i = 0; i < count; i += static_cast<uint32_t>(stride_)) {
      const uint8_t* p = base + static_cast<size_t>(i) * step;
      float x, y, z;
      std::memcpy(&x, p + ox, 4);
      std::memcpy(&y, p + oy, 4);
      std::memcpy(&z, p + oz, 4);
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
      if (z <= 0.02f) continue;  // drop the sensor origin / bogus zeros
      out.push_back(x); out.push_back(y); out.push_back(z);
      if (static_cast<int>(out.size()) / 3 >= max_points_) break;
    }

    std::lock_guard<std::mutex> lk(mu_);
    cloud_.swap(out);
  }

  void sendAll(int fd, const char* data, size_t len)
  {
    size_t sent = 0;
    while (sent < len) {
      ssize_t n = ::send(fd, data + sent, len - sent, 0);
      if (n <= 0) break;
      sent += static_cast<size_t>(n);
    }
  }

  void handle(int fd)
  {
    char req[1024];
    ssize_t n = ::recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    std::string header = "HTTP/1.0 200 OK\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Cache-Control: no-store\r\n"
                         "Connection: close\r\n";

    if (std::strncmp(req + 4, "/points", 7) == 0 || std::strstr(req, "/points") != nullptr) {
      std::vector<float> snapshot;
      {
        std::lock_guard<std::mutex> lk(mu_);
        snapshot = cloud_;
      }
      std::string head = header +
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(snapshot.size() * sizeof(float)) + "\r\n\r\n";
      sendAll(fd, head.data(), head.size());
      if (!snapshot.empty())
        sendAll(fd, reinterpret_cast<const char*>(snapshot.data()),
                snapshot.size() * sizeof(float));
    } else {
      std::string body = kHtml;
      std::string head = header +
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
      sendAll(fd, head.data(), head.size());
      sendAll(fd, body.data(), body.size());
    }
  }

  void serverLoop()
  {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { RCLCPP_ERROR(get_logger(), "socket() failed"); return; }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      RCLCPP_ERROR(get_logger(), "bind() to port %d failed", port_);
      ::close(srv);
      return;
    }
    ::listen(srv, 8);
    RCLCPP_INFO(get_logger(), "HTTP server listening on port %d", port_);

    while (running_) {
      int fd = ::accept(srv, nullptr, nullptr);
      if (fd < 0) continue;
      try { handle(fd); } catch (...) {}
      ::close(fd);
    }
    ::close(srv);
  }

  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_;
  std::string topic_;
  int port_{8766};
  int stride_{2};
  int max_points_{120000};
  std::mutex mu_;
  std::vector<float> cloud_;
  std::thread server_thread_;
  std::atomic<bool> running_{false};
};

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<CloudWebBridge>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
