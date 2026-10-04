@echo off
setlocal
echo ======================================================================
echo  Building NightWatch (MinGW g++ + Ninja)
echo ======================================================================
rem MinGW toolchain (g++, cmake, ninja). Falls back to whatever is on PATH.
if exist "C:\cpsetup-main\bin\g++.exe" set "PATH=C:\cpsetup-main\bin;%PATH%"
if exist "C:\msys64\ucrt64\bin\g++.exe" set "PATH=%PATH%;C:\msys64\ucrt64\bin"

cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 ( echo [ERROR] CMake configuration failed. & exit /b 1 )

cmake --build build-mingw -j
if errorlevel 1 ( echo [ERROR] Compilation failed. & exit /b 1 )

echo [BUILD SUCCESSFUL] build-mingw\NightWatch.exe
