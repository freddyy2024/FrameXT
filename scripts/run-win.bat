@echo off
rem Run framext.exe with full VS x64 + Qt env (local smoke test)
rem NOTE: keep this file ASCII-only
setlocal
call "D:\MyApp\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set "PATH=D:\MyApp\Qt\6.11.1\msvc2022_64\bin;%PATH%"
set "EXE=%~dp0..\build\win-debug\src\app\framext.exe"
if not exist "%EXE%" (
    echo [ERROR] exe not found: %EXE%
    exit /b 1
)
"%EXE%"
echo EXITCODE=%ERRORLEVEL%
