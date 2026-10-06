@echo off
REM ============================================================
REM  用 Docker 跑 Rockchip 官方 boot_merger，合成 RV1108(RK110A) 的
REM  MiniLoaderAll.bin。boot_merger 是 Linux x86-64 二进制，
REM  所以在 Docker 里用 linux/amd64 镜像跑。
REM
REM  用法：双击本文件，或在命令行里执行 build_loader.bat
REM  前提：已安装并启动 Docker Desktop
REM ============================================================
setlocal
set HERE=%~dp0
cd /d "%HERE%"

echo.
echo [1/4] 检查 Docker ...
docker version >nul 2>&1
if errorlevel 1 (
  echo    !! Docker 没装好或没启动。请先安装 Docker Desktop 并让它跑起来。
  pause
  exit /b 1
)
echo    OK

echo.
echo [2/4] 准备 rkbin 目录树 ...
if not exist "rkbin\tools\boot_merger" (
  echo    !! 找不到 rkbin\tools\boot_merger
  pause
  exit /b 1
)

echo.
echo [3/4] 打印 boot_merger 用法（确认参数写法）...
docker run --rm --platform linux/amd64 -v "%HERE%rkbin:/work" -w /work debian:bookworm-slim ^
  bash -c "chmod +x tools/boot_merger; ./tools/boot_merger 2>&1 | head -30; echo '---usage end---'"

echo.
echo [4/4] 合成 loader ...
docker run --rm --platform linux/amd64 -v "%HERE%rkbin:/work" -w /work debian:bookworm-slim ^
  bash -c "chmod +x tools/boot_merger; ./tools/boot_merger --pack --ini RKBOOT/RV110XMINIALL.ini --output /work/rv110x_loader_v1.12.126.bin; echo rc=$?; ls -la /work/*.bin"

echo.
echo 如果成功，产物在：%HERE%rkbin\rv110x_loader_v1.12.126.bin
echo 把它填进 RKDevTool 的 Boot 框，点【下载】试试。
echo.
pause
