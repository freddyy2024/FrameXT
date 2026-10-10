@echo off
rem M2: install exiv2 via new vcpkg (manifest mode), proxy for github
rem NOTE: keep this file ASCII-only
setlocal
set "HTTP_PROXY=http://127.0.0.1:7897"
set "HTTPS_PROXY=http://127.0.0.1:7897"
set "VCPKG_ROOT=D:\MyApp\vcpkg"
cd /d "%~dp0.."
D:\MyApp\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root="%CD%"
