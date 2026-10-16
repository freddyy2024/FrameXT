@echo off
rem FrameXT local build: VS x64 env + CMake Presets (win-debug)
rem Usage: scripts\build-win.bat [configure|build|test|all|release]  (default: all)
rem Requires: VS2022 (C++ workload, auto-located via vswhere), env QT_ROOT_DIR,
rem env VCPKG_ROOT (see README). Ninja comes from the VS developer environment.
rem NOTE: keep this file ASCII-only (cmd.exe parses batch in legacy codepage)
setlocal

rem ---- locate Visual Studio via vswhere (ships with the VS installer) ----
rem NOTE: no parenthesized blocks below -- variables containing "(x86)" in
rem their expansion would terminate the block early when echoed inside it.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vswhere
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%i"
if defined VS_ROOT goto :have_vs
echo [ERROR] No Visual Studio with C++ x64 tools found (vswhere)
exit /b 1
:no_vswhere
echo [ERROR] vswhere.exe not found. Install Visual Studio 2022 with the
echo         "Desktop development with C++" workload.
exit /b 1
:have_vs
set "VS_VCVARS=%VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat"

set "ROOT=%~dp0.."

call "%VS_VCVARS%" x64 >nul 2>&1
if not errorlevel 1 goto :vcvars_ok
echo [ERROR] Cannot load VS x64 env: %VS_VCVARS%
exit /b 1
:vcvars_ok

cd /d "%ROOT%"
set "STEP=%~1"
if "%STEP%"=="" set "STEP=all"

if "%STEP%"=="configure" goto :configure
if "%STEP%"=="build" goto :build
if "%STEP%"=="test" goto :test
if "%STEP%"=="all" goto :configure
if "%STEP%"=="release" goto :release
echo [ERROR] Unknown step: %STEP%
exit /b 1

:configure
cmake --preset win-debug || exit /b 1
if not "%STEP%"=="all" exit /b 0

:build
cmake --build --preset win-debug || exit /b 1
if not "%STEP%"=="all" exit /b 0

:test
ctest --preset win-debug || exit /b 1
exit /b 0

rem ---- win-release: packaging base. MUST run inside VS env (vcvars above):
rem ---- otherwise CMake falls back to MinGW gcc against MSVC Qt and breaks.
:release
cmake --preset win-release || exit /b 1
cmake --build --preset win-release || exit /b 1
ctest --preset win-release --output-on-failure || exit /b 1
