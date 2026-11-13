cmake_minimum_required(VERSION 3.21)
project(EYE59 VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ---- GStreamer ----
add_library(gst_deps INTERFACE)
if(WIN32)
    set(GST_ROOT "$ENV{GSTREAMER_1_0_ROOT_MINGW_X86_64}" CACHE PATH
        "Thu muc goc GStreamer (chua bin/, include/, lib/)")
    if(NOT EXISTS "${GST_ROOT}/include/gstreamer-1.0/gst/gst.h")
        message(FATAL_ERROR "Khong tim thay GStreamer tai GST_ROOT='${GST_ROOT}'")
    endif()
    target_include_directories(gst_deps INTERFACE
        "${GST_ROOT}/include/gstreamer-1.0"
        "${GST_ROOT}/include/glib-2.0"
        "${GST_ROOT}/lib/glib-2.0/include")
    # KHONG them ${GST_ROOT}/lib vao duong tim kiem: thu muc do co libmingwex/libstdc++/libgcc rieng
    # cua GStreamer (UCRT) se bi lay nham thay cho runtime cua trinh bien dich. Link dich danh file .dll.a.
    foreach(lib gstapp-1.0 gstvideo-1.0 gstbase-1.0 gstreamer-1.0 gobject-2.0 glib-2.0)
        target_link_libraries(gst_deps INTERFACE "${GST_ROOT}/lib/lib${lib}.dll.a")
    endforeach()
    target_link_libraries(gst_deps INTERFACE pdh psapi dwmapi crypt32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)
    target_link_libraries(gst_deps INTERFACE PkgConfig::GST)
endif()

add_executable(litepss WIN32
    src/main.cpp
    src/Config.h src/Config.cpp
    src/GstPlayer.h src/GstPlayer.cpp
    src/VideoTile.h src/VideoTile.cpp
    src/LiveView.h src/LiveView.cpp
    src/DahuaClient.h src/DahuaClient.cpp
    src/TimelineWidget.h src/TimelineWidget.cpp
    src/PlaybackView.h src/PlaybackView.cpp
    src/Metrics.h src/Metrics.cpp
    src/MetricsBar.h src/MetricsBar.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/Theme.h src/Theme.cpp
    src/ImageSettings.h src/ImageSettings.cpp
    src/AdjustDialog.h src/AdjustDialog.cpp
    src/CameraDirectory.h src/CameraDirectory.cpp
    src/OnvifClient.h src/OnvifClient.cpp
    src/LightProbe.cpp
    src/LightController.h src/LightController.cpp
    src/Users.h src/Users.cpp
    src/SettingsView.h src/SettingsView.cpp
    src/RecordCalendar.h src/RecordCalendar.cpp
    src/ClipExporter.h src/ClipExporter.cpp
    src/ExportDialog.h src/ExportDialog.cpp
    src/AppInfo.h src/AppInfo.cpp
    src/Updater.h src/Updater.cpp
    src/HeadDetector.h src/HeadDetector.cpp
    src/CountWorker.h src/CountWorker.cpp
    src/CountView.h src/CountView.cpp
)
set_target_properties(litepss PROPERTIES OUTPUT_NAME "59-eye")
set(UPDATE_URL "" CACHE STRING "Ghi de nguon cap nhat OTA (rong = repo chinh thuc phamchitai95/59-eye)")
target_compile_definitions(litepss PRIVATE APP_VERSION="${PROJECT_VERSION}" UPDATE_URL="${UPDATE_URL}")
# Logo: assets/icon.png (icon trong app, tao tu logo.png) va assets/59-eye.ico (icon file exe)
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/icon.png")
    set(CMAKE_AUTORCC ON)
    target_sources(litepss PRIVATE assets/assets.qrc)
endif()
if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/59-eye.ico")
        set(ICON_LINE "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/assets/59-eye.ico\"")
    else()
        set(ICON_LINE "")
    endif()
    configure_file(assets/app.rc.in "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
    target_sources(litepss PRIVATE "${CMAKE_BINARY_DIR}/app.rc")
endif()
if(WIN32)
    target_compile_definitions(litepss PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX UNICODE _UNICODE)
endif()
target_link_libraries(litepss PRIVATE Qt6::Widgets Qt6::Network gst_deps)

# ---- ONNX Runtime (dem nguoi): lien ket thuong voi onnxruntime.dll, chep canh file chay ----
# (Windows tim DLL o thu muc exe truoc System32 nen dung ban di kem, khong phai ban cu cua he thong)
set(ORT_ROOT "${CMAKE_SOURCE_DIR}/../_deps/onnxruntime-win-x64-1.31.1" CACHE PATH "Thu muc ONNX Runtime (include/, lib/)")
target_include_directories(litepss PRIVATE "${ORT_ROOT}/include")
if(WIN32)
    target_link_libraries(litepss PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib")
else()
    target_link_libraries(litepss PRIVATE onnxruntime)
endif()
if(WIN32 AND EXISTS "${ORT_ROOT}/lib/onnxruntime.dll")
    add_custom_command(TARGET litepss POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:litepss>")
endif()
