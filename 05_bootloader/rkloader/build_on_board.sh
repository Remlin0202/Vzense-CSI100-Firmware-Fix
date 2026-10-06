#!/bin/bash
# 在 Q6A 板子上合成 RV1108(RK110A) 的 MiniLoaderAll.bin
#
# 原理：boot_merger 是 Linux x86-64 二进制，板子是 ARM64，
#       用 qemu-user-static 做用户态模拟来跑它。
#
# 用法：把整个 rkloader 目录传到板子上，然后：
#   bash /home/a/rkloader/build_on_board.sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
echo "工作目录: $DIR"

echo
echo "=== 0. 检查 qemu-user-static ==="
if ! command -v qemu-x86_64-static >/dev/null 2>&1 && ! command -v qemu-x86_64 >/dev/null 2>&1; then
    echo "  没装。正在安装 ..."
    sudo apt-get update -qq
    sudo apt-get install -y qemu-user-static
fi
QEMU="$(command -v qemu-x86_64-static || command -v qemu-x86_64)"
echo "  qemu = $QEMU"

echo
echo "=== 1. 检查 rkbin 目录树 ==="
for f in tools/boot_merger RKBOOT/RV110XMINIALL.ini \
         bin/rv11/rv1108_ddr3_v1.12.bin bin/rv11/rv110x_miniloader_v1.26.bin \
         bin/rv11/rv110x_usbplug_v1.26.bin; do
    if [ -f "rkbin/$f" ]; then echo "  OK   $f"; else echo "  缺!! $f"; exit 1; fi
done

cd rkbin
chmod +x tools/boot_merger

echo
echo "=== 2. 打印 boot_merger 用法（确认 --pack 参数写法）==="
"$QEMU" ./tools/boot_merger 2>&1 | head -30 || true
echo "--- usage end ---"

echo
echo "=== 3. 合成 loader ==="
"$QEMU" ./tools/boot_merger --pack --ini RKBOOT/RV110XMINIALL.ini \
        --output rv110x_loader_v1.12.126.bin
echo "  rc=$?"
ls -la ./*.bin 2>/dev/null || true

echo
echo "=== 4. 结果 ==="
OUT=rv110x_loader_v1.12.126.bin
if [ -f "$OUT" ]; then
    SZ=$(stat -c %s "$OUT")
    echo "  产物: $DIR/rkbin/$OUT  ($SZ bytes)"
    echo "  头部:"
    xxd -l 64 "$OUT" || od -A x -t x1z -v "$OUT" | head -4
    echo
    echo "  → 把它拷回 PC，填进 RKDevTool 的 Boot 框，点【下载】"
else
    echo "  !! 没有产物，看看上面的报错"
fi
