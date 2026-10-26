@echo off
setlocal

if not exist "%~dp0build\CMakeCache.txt" (
    echo Configure the project first from this folder:
    echo cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    pause
    exit /b 1
)

cmake --build "%~dp0build" --config Debug --parallel 1 -- /nr:false
if errorlevel 1 (
    echo Build failed. See the messages above.
    pause
    exit /b 1
)

rem The starter project loads shaders and images relative to this directory.
pushd "%~dp0build\src"
if errorlevel 1 exit /b 1
"%~dp0bin\Debug\vulkan_grass_rendering.exe" %*
set "grassExitCode=%ERRORLEVEL%"
popd
if not "%grassExitCode%"=="0" pause
exit /b %grassExitCode%
