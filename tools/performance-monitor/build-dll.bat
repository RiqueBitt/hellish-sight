@echo off
echo Building perf-monitor.dll...
cd /d "%~dp0dll"

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake configure failed!
    pause
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Build successful!
echo DLL: %cd%\Release\perf-monitor.dll
pause
