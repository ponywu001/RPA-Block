@echo off
REM Configure + build with the VS 2022 Build Tools toolchain (bundled CMake/Ninja).
REM Usage:  build.cmd [extra cmake -D options...]
setlocal

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "%CMAKE%" (
    echo ERROR: bundled CMake not found at "%CMAKE%"
    exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed
    exit /b 1
)

"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    %*
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%~dp0build"
if errorlevel 1 exit /b 1

endlocal
