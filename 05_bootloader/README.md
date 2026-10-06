# 05_bootloader —— 刷机用 loader

## 用哪个？→ **`rkloader/loader_RK110A.bin`**

RKDevTool 的 `高级功能` → `Boot:` 选它 → 点 `下载`。

```
rkloader/loader_RK110A.bin    237,902 字节   md5 05debc9d6dc5f8c84aab162d619b5e3e
```

> 早期还打包过一份芯片 ID 写作 `RV1108` 的同款 loader（`loader_RV1108.bin`）。
> **实测两者都能正常载入**（这个字段只是标识，BROM 不会因为它不匹配就拒绝），
> 因为 `RK110A` 才是设备自报的标识，那份**已删除**，只留现在这一份。

### 为什么叫 RK110A（而不是 RV1108）

RKDevTool → `高级功能` → 读取 Chip 信息，**相机自己报出**的是：

```
获取ChipInfo开始
Chip Tag:        31 31 30 41        ← ASCII 就是 "110A"
Image Chip Flag: -RK110A
获取ChipInfo成功
```

`31 31 30 41` 就是 `110A` 四个字节。loader 内部这个字段是按**小端 u32** 存的
（字节序为 `41 30 31 31`），指的是同一个东西。

⇒ **`RK110A` 是这颗芯片的标识**；`RV1108` 是它的**型号名** —— 同一颗芯片的两个名字。
loader 的芯片 ID 字段就写成了设备自报的那个。

---

## 这个 loader 是怎么来的（**不是我们编译的**）

它是**用 Rockchip 官方 `boot_merger` 工具、把三个 Rockchip 预编译零件打包出来的**：

| 零件（来自 Rockchip rkbin） | 大小 | 作用 |
|---|---|---|
| `bin/rv11/rv1108_ddr3_v1.12.bin` | 5,736 B | DDR3 初始化 |
| `bin/rv11/rv110x_usbplug_v1.26.bin` | 112,168 B | USB 插件模式（在 BROM 里跑，负责接收） |
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
