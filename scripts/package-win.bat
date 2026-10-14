@echo off
rem FrameXT Windows packaging (M6.1): deploy assembly + Inno Setup installer.
rem Usage: scripts\package-win.bat   (assumes build-win.bat release already done;
rem        this script re-runs the release build to guarantee freshness)
rem NOTE: keep this file ASCII-only (cmd.exe parses batch in legacy codepage)
setlocal
set "VS_VCVARS=D:\MyApp\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "QT_BIN=D:\MyApp\Qt\6.11.1\msvc2022_64\bin"
set "ISCC=D:\Program Files (x86)\Inno Setup 6\ISCC.exe"
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

rem ---- 3) installer ----
echo [package] Inno Setup compile...
"%ISCC%" "packaging\framext.iss" || exit /b 1

echo [package] DONE: dist\FrameXT-*-setup.exe
exit /b 0
