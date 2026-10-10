@echo off
rem M2: install manifest deps via new vcpkg, proxy for github
rem M4: install root moved off the CJK project path -- link.exe reads
rem rsp files in the ANSI codepage, so any .lib path under a UTF-8
rem (Chinese) directory turns to mojibake and dies with LNK1104.
rem Must match VCPKG_INSTALLED_DIR in CMakePresets.json.
rem NOTE: keep this file ASCII-only
setlocal
set "HTTP_PROXY=http://127.0.0.1:7897"
set "HTTPS_PROXY=http://127.0.0.1:7897"
set "VCPKG_ROOT=D:\MyApp\vcpkg"
set "INSTALL_ROOT=D:\MyApp\vcpkg-installed\framext"
cd /d "%~dp0.."
D:\MyApp\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root="%CD%" --x-install-root="%INSTALL_ROOT%"
