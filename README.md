# LanCast

基于 C++ + Qt 的 Windows 局域网屏幕共享软件。无需中心服务器，局域网内自动发现房间，低延迟实时屏幕共享。

## 核心功能

- **创建房间**: 用户可创建房间并共享自己的屏幕
- **自动发现**: 局域网内其他客户端通过 UDP 广播自动发现房间
- **实时观看**: 加入房间后实时观看屏幕内容
- **低延迟**: 目标端到端延迟 < 200ms

## 技术架构

### 模块设计

```
┌─────────────────────────────────────────────────────────────────┐
│                        StreamEngine                              │
│                    (核心编排器 / Core Orchestrator)               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  HOST 模式:                                                      │
│  ┌──────────────┐    ┌─────────────┐    ┌──────────────────┐   │
│  │   Capture    │───>│   Encoder   │───>│  RTP Packetizer  │   │
│  │ (DXGI Dup)   │    │ (H.264/FFmpeg)   │                  │   │
│  └──────────────┘    └─────────────┘    └────────┬─────────┘   │
│                                                    │             │
│  ┌──────────────┐                                   │ UDP        │
│  │  Discovery   │<─────────────────────────────────┘             │
│  │ (UDP广播45678)│                                                   │
│  └──────────────┘                                                   │
│                                                                  │
│  VIEWER 模式:                                                      │
│  ┌──────────────┐    ┌─────────────────┐    ┌──────────────┐    │
│  │  Discovery   │    │RTP Depacketizer │    │   Decoder    │    │
│  │ (UDP广播45678)│───>│  (FU-A 重组)    │───>│ (H.264/FFmpeg)│   │
│  └──────────────┘    └─────────────────┘    └──────┬───────┘    │
│                                                     │              │
│                                              ┌──────▼───────┐    │
│                                              │ VideoWidget  │    │
│                                              │ (Qt 渲染)    │    │
│                                              └──────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 目录结构

```
LanCast/
├── include/lancast/
│   ├── Capture/
│   │   └── DesktopCapturer.h      # DXGI Desktop Duplication API
│   ├── Encoder/
│   │   └── H264Encoder.h         # FFmpeg H.264 编码
│   ├── Network/
│   │   ├── RtpPacketizer.h       # RTP 分片
│   │   ├── RtpDepacketizer.h     # RTP 重组
│   │   ├── UdpSocket.h           # UDP 套接字封装
│   │   └── NetworkManager.h      # 网络管理
│   ├── Discovery/
│   │   └── RoomDiscovery.h       # UDP 广播房间发现
│   ├── Decoder/
│   │   └── H264Decoder.h         # FFmpeg H.264 解码
│   ├── Render/
│   │   └── VideoWidget.h         # Qt 视频渲染控件
│   ├── Common/
│   │   ├── FrameBuffer.h         # 视频帧结构 (YUV420P)
│   │   ├── RtpPacket.h           # RTP 包结构 (RFC 3550)
│   │   ├── RoomInfo.h            # 房间信息结构
│   │   └── CircularBuffer.h      # 线程安全环形缓冲区
│   └── Core/
│       ├── FrameQueue.h          # 帧队列类型别名
│       └── StreamEngine.h        # 核心编排器
├── src/
│   ├── Capture/DesktopCapturer.cpp
│   ├── Encoder/H264Encoder.cpp
│   ├── Network/
│   │   ├── RtpPacketizer.cpp
│   │   ├── RtpDepacketizer.cpp
│   │   ├── UdpSocket.cpp
│   │   └── NetworkManager.cpp
│   ├── Discovery/RoomDiscovery.cpp
│   ├── Decoder/H264Decoder.cpp
│   ├── Render/VideoWidget.cpp
│   └── Core/StreamEngine.cpp
├── ui/
│   └── MainWindow.ui             # Qt UI 布局
├── CMakeLists.txt                # CMake 构建配置
└── lancast.pro                   # qmake 项目文件
```

## 依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| **Qt** | 6.11.0 | GUI、Network 模块 |
| **FFmpeg** | 6.x (BtbN) | libavcodec (H.264 编解码) |
| **MinGW GCC** | 13.1 | 编译器 |
| **Windows SDK** | 10.0+ | DXGI Desktop Duplication |

## 构建

### 环境要求

- Qt 6.11.0 (已测试)
- CMake 3.16+
- MinGW GCC 13.1+ (Qt 自带工具链)
- FFmpeg (WinGet 安装的 BtbN 版本)

### CMake 编译步骤

```bash
# 进入项目目录
cd LanCast

# 创建并进入构建目录
mkdir build && cd build

# 设置 Qt 工具链路径
set PATH=D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\mingw1310_64\bin;%PATH%

# 配置 CMake
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="D:\Qt\6.11.0\mingw_64\lib\cmake"

# 编译
mingw32-make -j4
```

### Qt 部署

编译后需要使用 `windeployqt` 部署 Qt 运行时：

```bash
cd build
D:\Qt\6.11.0\mingw_64\bin\windeployqt lancast.exe
```

### FFmpeg 运行时

运行前需要将 FFmpeg DLL 复制到程序目录：

```
avcodec-62.dll
avformat-62.dll
avutil-60.dll
swresample-6.dll
swscale-9.dll
```

或添加到系统 PATH 环境变量。

## 协议设计

### 房间发现协议 (UDP 端口 45678)

```
┌─────────────────────────────────────────────────────────────┐
│ 广播消息格式: VER(1) + TTL(1) + MSG_TYPE(1) + PAYLOAD      │
└─────────────────────────────────────────────────────────────┘

消息类型:
  0x01 - ADVERTISEMENT  (房间广播)
  0x02 - QUERY          (查询请求)
  0x03 - RESPONSE       (响应)

ADVERTISEMENT / RESPONSE Payload:
  RoomID(16) + RoomName(64) + HostName(32) + IP(4) + Port(2) + Version(1)

TTL: 每经过一个节点减 1，TTL=0 时停止转发
```

### RTP 流协议 (UDP 端口 50000-60000)

```
┌─────────────────────────────────────────────────────────────┐
│ RTP Header (12 bytes, RFC 3550)                            │
├─────────────────────────────────────────────────────────────┤
│  V(2) | P(1) | X(1) | CC(4) | M(1) |     PT(7)            │
│        Sequence Number (16)                                 │
│        Timestamp (32) - 90kHz 时钟                          │
│        SSRC (32)                                            │
├─────────────────────────────────────────────────────────────┤
│ Payload: H.264 NAL 单元或 FU-A 分片                         │
└─────────────────────────────────────────────────────────────┘

Payload Type: 96 (H.264)
Marker Bit: 1 表示帧结束
Timestamp: 90000 / fps 增量 (30fps = 3000/frame)
```

### H.264 分片策略

- **小 NAL (< 1400 bytes)**: 单包模式，直接发送
- **大 NAL (>= 1400 bytes)**: FU-A 分片，每片最大 1400 bytes

## 使用说明

### 共享屏幕 (Host)

1. 启动 LanCast
2. 在"共享屏幕 (Host)"标签页输入房间名称
3. 点击"创建房间"
4. 房间信息将通过 UDP 广播到局域网

### 观看屏幕 (Viewer)

1. 启动 LanCast
2. 切换到"观看屏幕 (Viewer)"标签页
3. 点击"刷新房间"发现局域网内的房间
4. 选择要加入的房间，点击"加入房间"
5. 屏幕共享将实时显示在窗口中央

## 线程模型

```
[Capture Thread]    ──VideoFrameQueue──> [Encode Thread] ──EncodedQueue──> [Network Thread]
   DXGI 30fps              │                     │                   UDP Send
                          v                     v                    |
                    [Frame Drop]          [H264Encoder]              │
                                                                     v
[Network Thread] ──RtpQueue──> [Depacketize] ──DecodedQueue──> [Render Thread]
   UDP Recv                      FU-A                 │              Qt UI
                                                    v
                                             [H264Decoder]
```

- **Capture Thread**: DXGI 屏幕采集，30fps，帧间隔 33ms
- **Encode Thread**: FFmpeg H.264 编码，ultrafast preset
- **Network Thread**: UDP 发送/接收，独立运行
- **Decode Thread**: FFmpeg H.264 解码
- **Render Thread**: Qt 主事件循环，绘制视频帧

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 采集分辨率 | 1920x1080 | 可配置 |
| 采集帧率 | 30 fps | 可配置 |
| 编码码率 | 2000 kbps | 可配置 |
| 编码Preset | ultrafast | 低延迟优先 |
| 发现端口 | 45678 | UDP广播 |
| 流端口 | 50000-60000 | UDP |

## 开发笔记

### DXGI Desktop Duplication

- 需要 D3D11 设备
- `IDXGIOutputDuplication::AcquireNextFrame()` 获取新帧
- 使用 Staging Texture CPU 读取像素
- BGRA → YUV420P 软件转换

### FFmpeg H.264

- 使用 `avcodec_send_frame` / `avcodec_receive_packet` API
- 输出 Annex B 格式 (0x00000001 start code)
- Profile: baseline (兼容性最佳)
- GOP: 2秒一个关键帧

### RTP over UDP

- 无连接，不可靠传输
- 依赖 UDP 本身的校验和
- 丢包处理: 解码器会自动跳过损坏帧
- 延迟优化: 不等待丢失的包

## 许可证

MIT License
