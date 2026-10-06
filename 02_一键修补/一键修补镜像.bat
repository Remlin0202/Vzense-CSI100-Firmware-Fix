@echo off
setlocal
rem ============================================================
rem  CSI100 firmware one-click patcher launcher (ASCII only)
rem  Drag the .img file onto this .bat, or just double-click it
rem  (it will look for a *.img next to it / in the package root).
rem  All guidance is printed by the Python script.
rem ============================================================

set "PYCMD="
where py >nul 2>nul
if %errorlevel%==0 set "PYCMD=py -3"
if not defined PYCMD (
    where python >nul 2>nul
    if %errorlevel%==0 set "PYCMD=python"
)
if not defined PYCMD goto :nopython

%PYCMD% "%~dp0csi100_patch_image.py" %*
if errorlevel 1 goto :failed

echo.
echo ============================================================
echo   Done. Flash the output image with RKDevTool:
echo     Download Image  -^>  tick "Force write by address"
echo     -^>  Address 0x00000000  -^>  select the image  -^>  Execute
echo   To recover, flash your original dump the same way.
echo ============================================================
pause
exit /b 0

:failed
echo.
echo [ERROR] Patching failed. See the Python messages above.
echo   - If it mentions "64-bit Python", install the 64-bit build.
echo   - If it says the image looks already patched, use your
echo     original dump as input.
pause
exit /b 1

:nopython
echo.
echo [ERROR] Python 3 not found (64-bit required).
echo.
pause
exit /b 1
