cmake_minimum_required(VERSION 3.16)
project(LanCast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find Qt
find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)

# FFmpeg paths - set via environment or cmake variable
set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "FFmpeg root directory")
if(NOT FFMPEG_ROOT)
    set(FFMPEG_ROOT "C:/Users/19848/AppData/Local/Microsoft/WinGet/Packages/BtbN.FFmpeg.GPL.Shared_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-N-123777-g53537f6cf5-win64-gpl-shared")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")

# Find FFmpeg libraries manually
find_library(AVCODEC_LIBRARY avcodec HINTS "${FFMPEG_LIB_DIR}")
find_library(AVFORMAT_LIBRARY avformat HINTS "${FFMPEG_LIB_DIR}")
find_library(AVUTIL_LIBRARY avutil HINTS "${FFMPEG_LIB_DIR}")
find_library(SWRESAMPLE_LIBRARY swresample HINTS "${FFMPEG_LIB_DIR}")
find_library(SWSCALE_LIBRARY swscale HINTS "${FFMPEG_LIB_DIR}")

set(FFMPEG_LIBRARIES
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
    ${SWSCALE_LIBRARY}
)

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIR}")

# Windows-specific
add_definitions(-DWIN32_LEAN_AND_MEAN)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DSTDC_HEADERS)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lancast
    ${FFMPEG_INCLUDE_DIRS}
)

# Source files (no Q_OBJECT classes - those go in executable)
set(COMMON_SOURCES
    src/Common/FrameBuffer.cpp
    src/Common/RtpPacket.cpp
    src/Common/RoomInfo.cpp
    src/Common/CircularBuffer.cpp
    src/Common/Logger.cpp
)

set(CORE_SOURCES
    src/Core/FrameQueue.cpp
)

set(CAPTURE_SOURCES
    src/Capture/DesktopCapturer.cpp
)

set(ENCODER_SOURCES
    src/Encoder/H264Encoder.cpp
)

set(NETWORK_SOURCES
    src/Network/RtpPacketizer.cpp
    src/Network/RtpDepacketizer.cpp
    src/Network/UdpSocket.cpp
    src/Network/NetworkManager.cpp
)

set(DECODER_SOURCES
    src/Decoder/H264Decoder.cpp
)

set(DISCOVERY_SOURCES
    src/Discovery/RoomDiscovery.cpp
)

# Library (without Q_OBJECT classes)
add_library(lancast_lib STATIC
    ${COMMON_SOURCES}
    ${CORE_SOURCES}
    ${CAPTURE_SOURCES}
    ${ENCODER_SOURCES}
    ${NETWORK_SOURCES}
    ${DECODER_SOURCES}
    ${RENDER_SOURCES}
    ${DISCOVERY_SOURCES}
)

target_link_libraries(lancast_lib
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ${FFMPEG_LIBRARIES}
    iphlpapi.lib
)

# Main executable - includes Q_OBJECT classes
set(MAIN_SOURCES
    src/main.cpp
    src/Core/StreamEngine.cpp
    src/Render/VideoWidget.cpp
)

set(UI_SOURCES
    ui/MainWindow.ui
)

# Generate MOC files explicitly for Q_OBJECT classes
qt6_generate_moc(include/lancast/Core/StreamEngine.h lancast_autogen/moc_StreamEngine.cpp)
qt6_generate_moc(include/lancast/Render/VideoWidget.h lancast_autogen/moc_VideoWidget.cpp)

add_executable(lancast
    ${MAIN_SOURCES}
    ${UI_SOURCES}
    lancast_autogen/moc_StreamEngine.cpp
    lancast_autogen/moc_VideoWidget.cpp
)

target_link_libraries(lancast
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    lancast_lib
)

