# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project Overview

LanCast is a Windows-only peer-to-peer LAN screen-sharing application built with C++17 and Qt 6. Hosts capture their desktop via DXGI, encode to H.264, and broadcast over RTP/UDP. Viewers discover rooms via UDP broadcast and decode/render the stream. Target latency is under 20ms. There is no central server.

## Build Commands

Uses CMake with MinGW. Qt 6 and FFmpeg must be installed separately.

**Full build** (run from repo root):
```bat
start.bat
```
This runs cmake + mingw32-make in the `build/` directory. Assumes Qt 6.11.0 at `D:\Qt\6.11.0\mingw_64` and MinGW at `D:\Qt\Tools\mingw1310_64`.

**Manual build:**
```bat
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:\Qt\6.11.0\mingw_64\lib\cmake" -DFFMPEG_ROOT="path/to/ffmpeg"
mingw32-make -j4
```

**Package release DLLs:**
```bat
package_release.bat
```
Copies exe + all required Qt, MinGW, and FFmpeg DLLs into `release/`.

An alternative `lancast.pro` (qmake) file exists but CMake is the primary build system.

## Architecture

### Pipeline Model

`StreamEngine` is the central orchestrator. It runs in one of three modes:

**HOST mode** — 3 threads connected by `CircularBuffer` queues:
```
Capture (DXGI) → [VideoFrameQueue] → Encode (FFmpeg H.264) → [EncodedFrameQueue] → Network Send (RTP/UDP)
```

**VIEWER mode** — 1 thread + Qt render signal:
```
Network Receive (RTP/UDP) → Depacketize (FU-A) → Decode (FFmpeg H.264) → newVideoFrame signal → VideoWidget paint
```

### Module Map

| Module | Header | Implementation | Role |
|---|---|---|---|
| **Core** | `StreamEngine.h` | `StreamEngine.cpp` | Pipeline orchestrator; owns all components, manages threads |
| **Capture** | `DesktopCapturer.h` | `DesktopCapturer.cpp` | DXGI Desktop Duplication → YUV420P frames |
| **Encoder** | `H264Encoder.h` | `H264Encoder.cpp` | FFmpeg H.264 encode (Annex B, baseline profile, ultrafast preset) |
| **Decoder** | `H264Decoder.h` | `H264Decoder.cpp` | FFmpeg H.264 decode |
| **Network** | `NetworkManager.h` | `NetworkManager.cpp` | RTP send/receive orchestrator |
| | `RtpPacketizer.h` | `RtpPacketizer.cpp` | NAL unit fragmentation into 1400-byte RTP payloads (FU-A) |
| | `RtpDepacketizer.h` | `RtpDepacketizer.cpp` | FU-A fragment reassembly |
| | `UdpSocket.h` | `UdpSocket.cpp` | Winsock2 UDP socket wrapper |
| **Discovery** | `RoomDiscovery.h` | `RoomDiscovery.cpp` | UDP broadcast room discovery (ports 45678-45688) |
| **Render** | `VideoWidget.h` | `VideoWidget.cpp` | Qt QWidget — YUV420P → RGB → paint |
| **Common** | `CircularBuffer.h` | `CircularBuffer.cpp` | Thread-safe ring buffer template (mutex + condvar) |
| | `FrameBuffer.h` | `FrameBuffer.cpp` | `VideoFrame` (YUV420P) and `EncodedFrame` structs |
| | `RtpPacket.h` | `RtpPacket.cpp` | RTP packet structure per RFC 3550 |
| | `RoomInfo.h` | `RoomInfo.cpp` | Room info serialization (119 bytes: RoomID, RoomName, HostName, IP, Port, Version) |
| | `Logger.h` | `Logger.cpp` | Static file logger |

### Entry Point

`src/main.cpp` contains both `main()` and the `MainWindow` class (QMainWindow with Q_OBJECT). The MainWindow UI is built programmatically — the `ui/MainWindow.ui` file exists as a reference/template but is not loaded at runtime.

### Threading & Data Flow

- `CircularBuffer<T>` is the shared queue between pipeline stages. It uses `std::mutex` + `std::condition_variable` for blocking push/pop.
- Type aliases: `VideoFrameQueue = CircularBuffer<VideoFramePtr>`, `EncodedFrameQueue = CircularBuffer<EncodedFramePtr>` (defined in `FrameQueue.h`).
- `VideoWidget` uses its own mutex for thread-safe frame handoff from the decoder thread.

### Key Protocols

- **RTP**: Payload type 96 (H.264), 90kHz clock, UDP ports 50000-60000
- **Discovery**: Custom binary protocol over UDP broadcast, 119-byte room info packets, ports 45678-45688

## Code Layout Notes

- Headers are in `include/lancast/`, sources in `src/`, organized by module subdirectory.
- The CMake build creates a static library `lancast_lib` for most modules, then links it into the `lancast` executable which includes Q_OBJECT classes with explicit MOC.
- Windows-specific: uses DXGI, D3D11, Winsock2. Not portable without replacing the Capture and Network modules.
- No test suite or CI/CD exists.
