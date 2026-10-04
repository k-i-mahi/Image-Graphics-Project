@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building NightWatch: Closed-Loop OpenGL and DIP Pipeline
echo  KUET CSE 4102 - Khadimul Islam Mahi (Roll: 2107076)
echo ======================================================================

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "!VCVARS!" (
    echo [WARNING] Default VS BuildTools not found at: !VCVARS!
    echo Attempting generic vcvarsall lookup...
    for %%p in (
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    ) do (
        if exist %%p (
            set "VCVARS=%%~p"
            goto :FOUND_VC
        )
    )
)

:FOUND_VC
echo Initializing MSVC environment via: "!VCVARS!"
call "!VCVARS!" x64

set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\msys64\ucrt64\bin;%PATH%"

if not exist build (
    mkdir build
)

echo Configuring with CMake and Ninja...
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b %ERRORLEVEL%
)

echo Building project...
cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ======================================================================
echo  [BUILD SUCCESSFUL] Executable generated: build\NightWatch.exe
echo  Run "run.bat" or launch .\build\NightWatch.exe
echo ======================================================================
