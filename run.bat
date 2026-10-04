@echo off
setlocal
cd /d "%~dp0"
call "%~dp0build.bat" nopause
if errorlevel 1 (
    pause
    exit /b 1
)
echo [INFO] Launching NightWatch...
start "NightWatch" /D "%~dp0build-mingw" "%~dp0build-mingw\NightWatch.exe"
