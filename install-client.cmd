@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SOURCE_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SOURCE_DIR%\build"
set "BUILD_CONFIG=Release"

if not defined VCPKG_ROOT (
    echo VCPKG_ROOT is not set.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target ircord-client
exit /b %errorlevel%
