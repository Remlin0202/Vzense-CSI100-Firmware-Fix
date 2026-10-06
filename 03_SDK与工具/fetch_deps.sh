#!/bin/bash
# ============================================================================
# fetch_deps.sh —— 拉取第三方依赖（Vzense 官方 SDK）
#
#   ★ 直接从【官方 GitHub】clone，不经过本仓库 —— 版权干净。
#     拉下来的目录已被 .gitignore 排除，不会误提交。
#
# 用法：  bash 03_SDK与工具/fetch_deps.sh
#         （在仓库根目录或本目录执行都行）
#
# 需要：  git；能访问 github.com
# ============================================================================
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

clone() {   # $1=仓库URL  $2=目标目录
    if [ -d "$2" ] && [ -n "$(ls -A "$2" 2>/dev/null)" ]; then
        echo "  已存在，跳过： $2"
        return 0
    fi
    echo "  clone $1"
    echo "     -> $DIR/$2"
    if git clone --depth 1 "$1" "$2" 2>/dev/null; then
        return 0
    fi
    # 有些人本地配了失效的 http.proxy（比如 Clash 没开），直连反而通
    echo "  直连失败，绕过本地代理再试一次 ..."
    rm -rf "$2"
    git -c http.proxy= -c https.proxy= clone --depth 1 "$1" "$2"
}

echo "=== Vzense 官方 SDK ==="
clone https://github.com/Vzense/Vzense_SDK_Windows.git VzenseSDK_Windows
clone https://github.com/Vzense/Vzense_SDK_Linux.git   VzenseSDK_Linux

echo
echo "=== Vzense UTool（Windows 验机工具，可选）==="
clone https://github.com/Vzense/UTool.git VzenseUTool

echo
echo "=== RKDevTool / DriverAssitant ==="
echo "  这两个【没有官方 git 仓库】，需要手动下载后放到本目录："
echo "    RKDevTool_Release.zip   ← 烧写工具"
echo "    DriverAssitant.zip      ← USB 驱动（要先装）"
echo "  下载页： https://docs.radxa.com/zero/zero3/low-level-dev/rkdevtool"
echo "         （或搜「RKDevTool 瑞芯微 官方」）"

echo
echo "=== 本目录现状 ==="
ls -1

echo
echo "完成后即可按 README 使用："
echo "  Windows 烧录  → 解压 DriverAssitant.zip 装驱动，用 RKDevTool"
echo "  ROS 2 编译    → export CSI100_SDK_ROOT=\"$DIR/VzenseSDK_Linux\""
