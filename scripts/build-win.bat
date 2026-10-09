@echo off
rem FrameXT local build: VS2022 x64 env + CMake Presets (win-debug)
rem Usage: scripts\build-win.bat [configure|build|test|all]  (default: all)
rem NOTE: keep this file ASCII-only (cmd.exe parses batch in legacy codepage)
setlocal
set "VS_VCVARS=D:\MyApp\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0.."

call "%VS_VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Cannot load VS2022 x64 env: %VS_VCVARS%
    exit /b 1
)

cd /d "%ROOT%"
set "STEP=%~1"
if "%STEP%"=="" set "STEP=all"

if "%STEP%"=="configure" goto :configure
if "%STEP%"=="build" goto :build
if "%STEP%"=="test" goto :test
if "%STEP%"=="all" goto :configure
echo [ERROR] Unknown step: %STEP%
exit /b 1

:configure
cmake --preset win-debug -DVCPKG_MANIFEST_INSTALL=OFF || exit /b 1
if not "%STEP%"=="all" exit /b 0

:build
cmake --build --preset win-debug || exit /b 1
if not "%STEP%"=="all" exit /b 0

:test
ctest --preset win-debug || exit /b 1
