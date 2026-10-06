# CSI100 固件修补分享包

> **Vzense CSI100**（板丝印 `boe cs100b4`，USB `2d40:0109`）—— Rockchip RV1108 方案的
> ToF 深度相机，二手拆机件。这颗相机的 UVC 描述符有厂商缺陷，导致 **Windows 上
> `usbvideo.sys` 拒绝启动（设备管理器 Code 10）**，官方 SDK 也因此完全看不到它。
>
> 本仓库给出：**烧录图文教程 + 一键修补脚本 + ROS 2 驱动 + 自己打包的烧写用 loader**。
> 第三方 SDK 与烧写工具的官方获取渠道见 `03_SDK与工具/获取说明.md`（**不随仓库分发**）。
> 修完之后 **Windows 与 Linux 都能正常使用**（Linux 侧本来就能用，修补后无回归）。

---

## 包结构

| 目录 / 文件 | 内容 |
|---|---|
| **`README.md`** / `LICENSE` | 本说明 / 许可（原创部分 MIT） |
| **`01_文档/`** | **`CSI100_烧录图文教程.pdf`（烧录图文教程，先看这个）** |
| **`02_一键修补/`** | `一键修补镜像.bat` + `csi100_patch_image.py`（一键修补）+ `_hash_recalc.py` / `_verify_e2e.py`（验证）+ `_fw_artifacts/`（LZO 库与包装源码） |
| **`03_SDK与工具/`** | **`获取说明.md`（第三方 SDK / 工具的官方下载渠道）+ `fetch_deps.sh` / `fetch_deps.bat`（一键 clone 官方 SDK）+ `sdk_probe.py`（验收探针）**。⚠️ Vzense SDK、RKDevTool、DriverAssitant **不随仓库分发** —— 版权归各自厂商、无再分发授权，请跑一下 `fetch_deps` 或按 `获取说明.md` 自行下载 |
| **`04_ROS2驱动/`** | `csi100_ros2/` —— 我们写的 ROS 2 包 `csi100_driver`（深度/IR/点云 + Web 点云桥） |
| **`05_bootloader/`** | `rkloader/loader_RK110A.bin` —— 刷机「下载 Boot」用的 loader（我们打包的） |

---

## 快速开始（Windows，从零到能用约 20 分钟）

```
⓪ 拉依赖        双击 03_SDK与工具/fetch_deps.bat（自动 clone 官方 SDK；
                并提示 RKDevTool / DriverAssitant 的下载页）
① 装驱动        解压 03_SDK与工具/DriverAssitant.zip → 运行 InstallDriver.exe
② 进 maskrom    相机断电 → 按住 USB 小板上按键不松 → 插 12V → 插 USB → 保持 15s → 松开
                （RKDevTool 底部应显示「发现一个 MASKROM 设备」）
③ 下载 Boot     RKDevTool → 高级功能 → Boot: 05_bootloader/rkloader/loader_RK110A.bin → 下载
④ 导出原始镜像  RKDevTool → 高级功能 → 导出镜像（起始扇区 0，扇区数 220672）
                → 立刻复制出来，改名 CSI100_原始固件.img  ★ 这是唯一的救砖文件
⑤ 打补丁        双击 02_一键修补/一键修补镜像.bat（会自动找到 ④ 的镜像；
                也可把镜像文件直接拖到 bat 上）→ 产出 CSI100_patched.img
⑥ 整片写入      RKDevTool → 下载镜像 → 勾「强制按地址写」→ 地址 0x00000000
                → 选 CSI100_patched.img → 执行
⑦ 断电重启      不碰按键
⑧ 验收          用 Vzense Utool 能检测到设备、能看到图 → 成功（图文步骤见 01_文档/）
```

**出问题就整片写回第 ④ 步导出的原始镜像**（同样流程，地址 `0x0`）即可恢复。

> 前置要求只有一样：**64 位 Python 3**（https://www.python.org/downloads/ ，安装时勾
> "Add Python to PATH"）。相机本身需要 **12~24V / 峰值 3A** 外接电源，USB 5V 只够枚举。

---

## ROS 2（Linux，可选）

包内 `04_ROS2驱动/csi100_ros2/` 是 ROS 2 Jazzy 的 `csi100_driver`（深度/IR/点云 + 浏览器看
3D 点云）。SDK 已内置，编译前二选一指定位置：

```bash
export CSI100_SDK_ROOT="$(pwd)/03_SDK与工具/VzenseSDK_Linux"   # 在包根目录执行
# 或：cd 04_ROS2驱动/csi100_ros2 && ln -s ../../03_SDK与工具/VzenseSDK_Linux sdk

cd ~/ros2_ws && colcon build --packages-select csi100_driver
source install/setup.bash
ros2 launch csi100_driver csi100_driver.launch.py
```

---

## 本仓库不含什么（以及为什么）

| 不含 | 原因 | 怎么获得 |
|---|---|---|
| **固件镜像**（原始 / 补丁后） | 设备厂商固件，有版权；且每台设备应各自从自己机器上取一份 | 用 RKDevTool 从**你自己的设备**导出（快速开始第 ④ 步），再用本仓库脚本打补丁 |
| **Vzense SDK**（Windows / Linux） | 第三方 SDK，无再分发授权 | 官方 GitHub：<https://github.com/Vzense/Vzense_SDK_Windows> 、<https://github.com/Vzense/Vzense_SDK_Linux> |
| **RKDevTool / DriverAssitant** | 瑞芯微工具，无再分发授权 | 瑞芯微官方发布页；或见 Radxa 文档 <https://docs.radxa.com/zero/zero3/low-level-dev/rkdevtool> |

上面这些下载后**放进 `03_SDK与工具/` 对应位置**即可（仓库的 `.gitignore` 已排除它们，
不会误提交）。详见 `03_SDK与工具/获取说明.md`。

> `02_一键修补/_fw_artifacts/` 里的 LZO DLL（GPL）**是随仓库提供的**，包装源码 `wrap999.c`
> 也在同目录；上游源码：<https://www.oberhumer.com/opensource/lzo/> 。

---

## 技术要点（三句话）

1. **根因**：UVC 描述符 `VS_FORMAT_UNCOMPRESSED.bNumFrameDescriptors` 声明 **2**、
   实际只发 **1** 个帧描述符 → Windows `usbvideo.sys` 零容忍 → Code 10。
2. **修法**：内核 **1 个字节**（vmlinux `0x06352AA`：`0x02 → 0x01`）。
3. **坑**：改内核后必须**重算 bootloader 的校验哈希**，否则 bootloader 拒绝启动。
   哈希 = `SHA256(数据 ‖ 头部 12 字节)`，而且 **kernel 和 kernel_1 两个槽都要改、都要重算**。
   一键脚本已自动处理（含 LZO1X-999 level 9 等长重压、53 字节尾部元数据保留）。

细节见 `01_文档/CSI100_烧录图文教程.pdf`。

---

## 许可

原创部分（脚本、ROS 2 驱动、自己打包的 loader、教程）可自由使用与分发；第三方组件遵循各自许可。
详见 `LICENSE`。
