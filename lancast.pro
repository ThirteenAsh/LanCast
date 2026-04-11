QT       += core widgets network

CONFIG += c++17

DEFINES += WIN32_LEAN_AND_MEAN

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/include/lancast \
    $$PWD/include/lancast/Capture \
    $$PWD/include/lancast/Encoder \
    $$PWD/include/lancast/Network \
    $$PWD/include/lancast/Discovery \
    $$PWD/include/lancast/Decoder \
    $$PWD/include/lancast/Render \
    $$PWD/include/lancast/Common \
    $$PWD/include/lancast/Core

# FFmpeg
FFMPEG_LIBS = avcodec avutil avformat

LIBS += -l$$join(FFMPEG_LIBS, " -l") -ld3d11 -ldxgi -ldxguid

# Windows SDK for DXGI
LIBS += -lstrmiids -ld3d11 -ldxgi -ldxguid

SOURCES += \
    src/Common/FrameBuffer.cpp \
    src/Common/RtpPacket.cpp \
    src/Common/RoomInfo.cpp \
    src/Common/CircularBuffer.cpp \
    src/Core/FrameQueue.cpp \
    src/Core/StreamEngine.cpp \
    src/Capture/DesktopCapturer.cpp \
    src/Encoder/H264Encoder.cpp \
    src/Network/RtpPacketizer.cpp \
    src/Network/RtpDepacketizer.cpp \
    src/Network/UdpSocket.cpp \
    src/Network/NetworkManager.cpp \
    src/Decoder/H264Decoder.cpp \
    src/Render/VideoWidget.cpp \
    src/Discovery/RoomDiscovery.cpp \
    ui/MainWindow.ui \
    src/main.cpp

HEADERS += \
    include/lancast/Common/FrameBuffer.h \
    include/lancast/Common/RtpPacket.h \
    include/lancast/Common/RoomInfo.h \
    include/lancast/Common/CircularBuffer.h \
    include/lancast/Core/FrameQueue.h \
    include/lancast/Core/StreamEngine.h \
    include/lancast/Capture/DesktopCapturer.h \
    include/lancast/Encoder/H264Encoder.h \
    include/lancast/Network/RtpPacketizer.h \
    include/lancast/Network/RtpDepacketizer.h \
    include/lancast/Network/UdpSocket.h \
    include/lancast/Network/NetworkManager.h \
    include/lancast/Decoder/H264Decoder.h \
    include/lancast/Render/VideoWidget.h \
    include/lancast/Discovery/RoomDiscovery.h

# UI
FORMS += ui/MainWindow.ui
