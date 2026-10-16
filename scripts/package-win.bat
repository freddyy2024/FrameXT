@echo off
rem FrameXT Windows packaging (M6.1): deploy assembly + Inno Setup installer.
rem Usage: scripts\package-win.bat   (assumes build-win.bat release already done;
rem        this script re-runs the release build to guarantee freshness)
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

rem ---- Qt via QT_ROOT_DIR; Inno Setup 6 via ISCC_EXE (override) or default path ----
if defined QT_ROOT_DIR goto :have_qt
echo [ERROR] QT_ROOT_DIR is not set. Point it to your Qt msvc directory, e.g.
echo         setx QT_ROOT_DIR "C:\Qt\6.8.0\msvc2022_64"
exit /b 1
:have_qt
set "QT_BIN=%QT_ROOT_DIR%\bin"
if exist "%QT_BIN%\windeployqt.exe" goto :have_qtbin
echo [ERROR] windeployqt.exe not found under QT_ROOT_DIR: %QT_BIN%
exit /b 1
:have_qtbin
if defined ISCC_EXE goto :have_iscc
set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
:have_iscc
if exist "%ISCC_EXE%" goto :have_tools
echo [ERROR] ISCC.exe not found: %ISCC_EXE% (install Inno Setup 6 or set ISCC_EXE)
exit /b 1
:have_tools

set "ROOT=%~dp0.."
set "DEPLOY=%ROOT%\build\deploy"

call "%VS_VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Cannot load VS2022 x64 env
    exit /b 1
)

cd /d "%ROOT%"

rem ---- 1) fresh release build + tests ----
call scripts\build-win.bat release || exit /b 1

rem ---- 2) deploy assembly ----
if exist "%DEPLOY%" rmdir /s /q "%DEPLOY%"
mkdir "%DEPLOY%" || exit /b 1
copy /y "build\win-release\src\app\framext.exe" "%DEPLOY%\" >nul || exit /b 1
copy /y "build\win-release\src\app\*.dll" "%DEPLOY%\" >nul || exit /b 1
echo [package] windeployqt...
"%QT_BIN%\windeployqt.exe" --release --no-translations --no-system-d3d-compiler ^
    --no-opengl-sw --no-quick-import "%DEPLOY%\framext.exe" || exit /b 1

rem ---- prune modules the app never uses (windeployqt over-copies) ----
rem dxcompiler/dxil (D3D shader for Qt Quick, unused), Qt6Pdf (print/pdf),
rem Qt6Svg + svg imageformats (no svg assets), Qt6Network + tls plugins.
rem Verified: app runs with JPEG/PNG/WebP/TIFF IO + windows style only.
del /q "%DEPLOY%\Qt6Pdf.dll" 2>nul
del /q "%DEPLOY%\Qt6Svg.dll" 2>nul
del /q "%DEPLOY%\Qt6Network.dll" 2>nul
del /q "%DEPLOY%\dxcompiler.dll" 2>nul
del /q "%DEPLOY%\dxil.dll" 2>nul
del /q "%DEPLOY%\imageformats\qpdf.dll" 2>nul
del /q "%DEPLOY%\imageformats\qsvg.dll" 2>nul
if exist "%DEPLOY%\tls" rmdir /s /q "%DEPLOY%\tls"

rem open-source licenses ride in the deploy tree so the installer and the
rem portable zip share one source of truth (iss packs the whole tree)
xcopy /e /i /y "licenses" "%DEPLOY%\licenses" >nul || exit /b 1
rem GPLv3 copy obligation: the license text itself must ship with binaries
copy /y "LICENSE" "%DEPLOY%\LICENSE" >nul || exit /b 1

rem ---- 3) installer ----
echo [package] Inno Setup compile...
"%ISCC_EXE%" "packaging\framext.iss" || exit /b 1

rem ---- 4) portable zip: deploy tree + marker + readme ----
rem marker file routes app data to <exeDir>\Data at runtime (core/AppPaths)
rem version read from exe VERSIONINFO (single source: root CMakeLists project())
for /f %%v in ('powershell -NoProfile -Command "(Get-Item '%DEPLOY%\framext.exe').VersionInfo.ProductVersion"') do set "APP_VER=%%v"
if not defined APP_VER (
    echo [ERROR] Cannot read exe ProductVersion
    exit /b 1
)
echo [package] portable zip (v%APP_VER%)...
copy /y "packaging\portable-readme.txt" "%DEPLOY%\README.txt" >nul || exit /b 1
type nul > "%DEPLOY%\FrameXT.portable"
set "PORTABLE_ZIP=%ROOT%\dist\FrameXT-%APP_VER%-portable.zip"
powershell -NoProfile -Command "Compress-Archive -Path $env:DEPLOY\* -DestinationPath $env:PORTABLE_ZIP -Force" || exit /b 1
del "%DEPLOY%\FrameXT.portable" "%DEPLOY%\README.txt" >nul 2>&1

echo [package] DONE: dist\FrameXT-%APP_VER%-setup.exe + dist\FrameXT-%APP_VER%-portable.zip
exit /b 0
