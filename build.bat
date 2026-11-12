@echo off
rem Build 59-eye tren Windows (MinGW). Sua 3 duong dan duoi neu cai o cho khac.
set QT_DIR=%~dp0..\_deps\Qt\6.8.3\mingw_64
set MINGW_DIR=%~dp0..\_deps\Qt\Tools\mingw1310_64
set GST_ROOT=%~dp0..\_deps\gst\PFiles64\gstreamer\1.0\mingw_x86_64
set CMAKE_BIN=%~dp0..\_deps\venv\Scripts

set PATH=%MINGW_DIR%\bin;%QT_DIR%\bin;%CMAKE_BIN%;%PATH%
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT_DIR%" -DGST_ROOT="%GST_ROOT%" || exit /b 1
cmake --build "%~dp0build" || exit /b 1
if not exist "%~dp0build\config.json" if exist "%~dp0config.json" copy "%~dp0config.json" "%~dp0build\" >nul
echo.
echo Build xong: %~dp0build\59-eye.exe  -- chay bang run.bat
