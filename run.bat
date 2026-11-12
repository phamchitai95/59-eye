@echo off
rem Chay 59-eye voi Qt + GStreamer trong PATH (ban PoC, chua dong goi)
set QT_DIR=%~dp0..\_deps\Qt\6.8.3\mingw_64
set MINGW_DIR=%~dp0..\_deps\Qt\Tools\mingw1310_64
set GST_ROOT=%~dp0..\_deps\gst\PFiles64\gstreamer\1.0\mingw_x86_64
set PATH=%GST_ROOT%\bin;%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%
set GST_PLUGIN_PATH=%GST_ROOT%\lib\gstreamer-1.0
cd /d "%~dp0build"
start "" 59-eye.exe
