@echo off
setlocal
rem Always work from the folder this script lives in (it may be started from anywhere)
cd /d "%~dp0"

echo ======================================================================
echo  Building NightWatch (MinGW-w64 g++ + CMake + Ninja)
echo ======================================================================

rem Known MinGW toolchain locations are added to PATH if present
if exist "C:\cpsetup-main\bin\g++.exe" set "PATH=C:\cpsetup-main\bin;%PATH%"
if exist "C:\msys64\ucrt64\bin\g++.exe" set "PATH=%PATH%;C:\msys64\ucrt64\bin"
if exist "C:\msys64\mingw64\bin\g++.exe" set "PATH=%PATH%;C:\msys64\mingw64\bin"

where g++ >nul 2>nul || (echo [ERROR] g++ not found. Install MinGW-w64 ^(e.g. MSYS2 UCRT64^) and add its bin folder to PATH. & goto :fail)
where cmake >nul 2>nul || (echo [ERROR] cmake not found. Install CMake 3.15+ and add it to PATH. & goto :fail)
where ninja >nul 2>nul || (echo [ERROR] ninja not found. Install Ninja and add it to PATH. & goto :fail)

rem GLFW is a git submodule: fetch it if the folder is empty
if not exist "extern\glfw\CMakeLists.txt" (
    echo [INFO] GLFW submodule missing - fetching it...
    git submodule update --init --recursive || (echo [ERROR] Run: git submodule update --init --recursive & goto :fail)
)

cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 (
    echo [INFO] Configure failed, retrying with a fresh CMake cache...
    cmake --fresh -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
    if errorlevel 1 (echo [ERROR] CMake configuration failed. & goto :fail)
)

cmake --build build-mingw -j
if errorlevel 1 (echo [ERROR] Compilation failed - see the messages above. & goto :fail)

echo.
echo [BUILD SUCCESSFUL] build-mingw\NightWatch.exe
echo Run it with run.bat (or .\run.bat in PowerShell).
exit /b 0

:fail
echo.
echo [BUILD FAILED]
rem Keep the window open when the script was double-clicked
if /i not "%~1"=="nopause" pause
exit /b 1
