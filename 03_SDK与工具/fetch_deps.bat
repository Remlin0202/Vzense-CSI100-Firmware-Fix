@echo off
chcp 65001 >nul
cd /d "%~dp0"

REM ==========================================================================
REM  fetch_deps.bat —— 拉取第三方依赖（Vzense 官方 SDK）
REM
REM    * 直接从【官方 GitHub】clone，不经过本仓库 —— 版权干净。
REM      拉下来的目录已被 .gitignore 排除，不会误提交。
REM
REM  用法： 双击本文件
REM  需要： git（https://git-scm.com/download/win）；能访问 github.com
REM ==========================================================================

echo ============================================================
echo   拉取第三方依赖
echo ============================================================
echo.

where git >nul 2>nul
if errorlevel 1 (
    echo [错误] 找不到 git。请先安装： https://git-scm.com/download/win
    echo.
    pause
    exit /b 1
)

echo === Vzense 官方 SDK ===
call :clone https://github.com/Vzense/Vzense_SDK_Windows.git VzenseSDK_Windows
call :clone https://github.com/Vzense/Vzense_SDK_Linux.git   VzenseSDK_Linux

echo.
echo === Vzense UTool（Windows 验机工具，可选）===
call :clone https://github.com/Vzense/UTool.git VzenseUTool

echo.
echo === RKDevTool / DriverAssitant ===
echo   这两个【没有官方 git 仓库】，需要手动下载后放到本目录：
echo     RKDevTool_Release.zip   ^<- 烧写工具
echo     DriverAssitant.zip      ^<- USB 驱动（要先装）
echo   下载页： https://docs.radxa.com/zero/zero3/low-level-dev/rkdevtool
echo          （或搜「RKDevTool 瑞芯微 官方」）

echo.
echo === 本目录现状 ===
dir /b

echo.
echo 完成后即可按 README 使用：
echo   Windows 烧录 -^> 解压 DriverAssitant.zip 装驱动，用 RKDevTool
echo   ROS 2 编译   -^> 指向 VzenseSDK_Linux 目录
echo.
pause
exit /b 0

:clone
if exist "%~2" (
    echo   已存在，跳过： %~2
    goto :eof
)
echo   clone %~1
echo      -^> %~dp0%~2
git clone --depth 1 %~1 %~2
if errorlevel 1 (
    echo   直连失败，绕过本地代理再试一次 ...
    if exist "%~2" rmdir /s /q "%~2" 2>nul
    git -c http.proxy= -c https.proxy= clone --depth 1 %~1 %~2
)
goto :eof
