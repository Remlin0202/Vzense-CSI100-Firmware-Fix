# 05_bootloader —— 刷机用 loader

## 用哪个？→ **两个都能用**；推荐 `rkloader/loader_RK110A.bin`

RKDevTool 的 `高级功能` → `Boot:` 选它 → 点 `下载`。

| 文件 | 芯片 ID 字段 | 说明 |
|---|---|---|
| **`rkloader/loader_RK110A.bin`** | `110A` | ★ **推荐** —— 与设备自报的 Chip Tag 一致 |
| `rkloader/loader_RV1108.bin` | `1108` | 同样可用；**图文教程里用的就是这份** |

**证据**（RKDevTool → `高级功能` → 读取 Chip 信息，由相机自己报出）：

```
Chip Tag:        31 31 30 41        ← ASCII 就是 "110A"
Image Chip Flag: -RK110A
```

⇒ 芯片的**标识是 `RK110A`**；`RV1108` 是它的**型号名**，两者是同一颗芯片。

**实测两个 loader 都能正常载入** —— 这个字段只是标识，BROM **不会**因为它不匹配就拒绝
（否则 `RV1108` 那份也烧不进去）。所以**随便用哪个都行**；推荐 `RK110A` 那份，
因为它和设备自报的标识对得上。

### 两者的差异（只差 5 个字节）

| 偏移 | `loader_RV1108.bin` | `loader_RK110A.bin` | 含义 |
|---|---|---|---|
| `0x15` | `'8'` | `'A'` | 芯片 ID 字符串（按字节反序读 = `1108` / `110A`） |
| `0x3A14A~0x3A14D` | `10 33 04 C5` | `C7 CC 9C 60` | 尾部 4 字节校验（上面那个字段变了，校验跟着变） |

两个文件都是 **237,902 字节**；md5 分别是 `06db4a378db3313c0c2af0bb83413182` /
`05debc9d6dc5f8c84aab162d619b5e3e`。

---

## 这个 loader 是怎么来的（**不是我们编译的**）

它是**用 Rockchip 官方 `boot_merger` 工具、把三个 Rockchip 预编译零件打包出来的**：

| 零件（来自 Rockchip rkbin） | 大小 | 作用 |
|---|---|---|
| `bin/rv11/rv1108_ddr3_v1.12.bin` | 5,736 B | DDR3 初始化 |
| `bin/rv11/rv110x_usbplug_v1.26.bin` | 112,168 B | USB 插件模式（BROM 里跑，负责接收） |
| `bin/rv11/rv110x_miniloader_v1.26.bin` | 112,588 B | miniloader（载入 DDR 后运行，提供读写 flash 的能力） |

三件之和 = 230,492 B，`boot_merger` 加上组件头后 ≈ **237,902 B** ✓
产物开头是 **`BOOT` 魔数**（boot_merger 的标准格式）。

**我们做的只有两件事**：① 选对零件与参数；② 让这个 Linux-only 工具在 ARM 板子上跑起来
（`qemu-user-static` 用户态模拟；Windows 上则用 Docker）。

打包命令（见 `build_on_board.sh` / `build_loader.bat`）：

```bash
qemu-x86_64-static ./tools/boot_merger pack -c -RV1108 -v 1.4 \
  -1 bin/rv11/rv1108_ddr3_v1.12.bin \
  -2 bin/rv11/rv110x_usbplug_v1.26.bin \
  -3 bin/rv11/rv1108_ddr3_v1.12.bin \
  -3 bin/rv11/rv110x_miniloader_v1.26.bin -o out.bin
```
⚠️ `-c` 的值**必须带前导横线**；必须有 `pack` 子命令；v1.38 不吃 `.ini`。

> 另：`boot_merger` 打包时还会**加密**这些组件，所以零件本身无法自行复现 ——
> 只能用 Rockchip 提供的二进制。

---

## 它和"相机自己那个 loader"不是一回事

| | 说明 |
|---|---|
| **本目录的 loader** | 刷机时**临时载入内存**（RKDevTool 的「下载 Boot」= LoaderToDDR，**不写 flash**），作用是**解锁读写能力** |
| **相机自己的 loader** | 在 flash 的 `IDBlock` 区（0x8000，明文），是厂商的，负责校验内核哈希并启动内核。我们只**读**过它（反汇编读出校验公式），**从没改过** |

---

## 许可

loader 及其 rkbin 零件均为 **Rockchip 的二进制**，版权归 Rockchip。
本项目只是"打包"，**未编写或编译任何 loader 代码**，因此**无权对其授予 MIT 许可** ——
详见仓库根目录 `LICENSE` 里的「不在 MIT 覆盖范围内」一节。
上游来源：https://github.com/rockchip-linux/rkbin
