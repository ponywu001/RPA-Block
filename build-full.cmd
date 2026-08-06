@echo off
REM Configure + build every module, using the prebuilt dependencies under C:\deps
REM and C:\Qt. Pass extra -D options through, e.g.:
REM     build-full.cmd -DRPA_BUILD_STUDIO=OFF
setlocal

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

set "QT_ROOT=C:\Qt\6.8.3\msvc2022_64"
set "OPENCV_ROOT=C:\deps\opencv\build"
set "ORT_ROOT=C:\deps\ort\onnxruntime-win-x64-1.20.1"

for %%D in ("%QT_ROOT%" "%OPENCV_ROOT%" "%ORT_ROOT%") do (
    if not exist "%%~D" (
        echo ERROR: dependency missing: %%~D
        echo See README.md for how to fetch the prebuilt dependencies.
        exit /b 1
    )
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

"%CMAKE%" -S "%~dp0." -B "%~dp0build-full" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
    -DOpenCV_DIR="%OPENCV_ROOT%" ^
    -DONNXRUNTIME_INCLUDE_DIR="%ORT_ROOT%\include" ^
    -DONNXRUNTIME_LIBRARY="%ORT_ROOT%\lib\onnxruntime.lib" ^
    -DRPA_BUILD_VISION=ON ^
    -DRPA_BUILD_STUDIO=ON ^
    %*
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%~dp0build-full"
if errorlevel 1 exit /b 1

REM Mirror the OCR models next to the exe so the unpackaged build finds them the
REM same way the packaged one does -- the app looks for models\ beside itself.
if exist "%~dp0models\rec.onnx" (
    if not exist "%~dp0build-full\bin\models" mkdir "%~dp0build-full\bin\models"
    copy /y "%~dp0models\*.onnx" "%~dp0build-full\bin\models\" >nul
    copy /y "%~dp0models\keys.txt" "%~dp0build-full\bin\models\" >nul
    if exist "%~dp0models\NOTICE.txt" copy /y "%~dp0models\NOTICE.txt" "%~dp0build-full\bin\models\" >nul
)

endlocal
