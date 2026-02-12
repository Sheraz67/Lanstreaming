# lancast - LAN Screen + Audio Streaming App

## Context
Build a native C++ desktop app for streaming screen + audio over LAN (like Discord screen share but for local networks). Manual IP entry for discovery. **Linux first** (X11), then Windows, then macOS later.

## Tech Stack
- **Screen Capture**:
  - Linux: X11 XShm (X Shared Memory) for fast full-screen capture
  - Windows (later): DXGI Desktop Duplication API
  - macOS (later): ScreenCaptureKit
- **Audio Capture**:
  - Linux: PulseAudio monitor source (captures system audio output)
  - Windows (later): WASAPI loopback
  - macOS (later): ScreenCaptureKit audio
- **Video Encoding**: FFmpeg `libx264` (software, universally available) with optional VAAPI HW accel on Linux
- **Audio Encoding**: Opus via FFmpeg `libopus`
- **Network**: Custom UDP protocol (no WebRTC/SRT overhead for LAN)
- **Rendering**: SDL3 (cross-platform window + texture + audio playback)
- **UI**: SDL3 minimal (host/join dialog with IP entry)
- **Build**: CMake 3.21+ with FetchContent

## Architecture Overview

### Host Pipeline (5 threads)
```
X11 XShm capture -> [queue] -> VideoEncoder(H.264) -> [queue] -> UDP Server
PulseAudio mon   -> [queue] -> AudioEncoder(Opus)  -> [queue] ----^
```

### Client Pipeline (4 threads + SDL audio thread)
```
UDP recv -> PacketAssembler -> [queue] -> VideoDecoder -> [queue] -> SDL3 Renderer
                            -> [queue] -> AudioDecoder -> [queue] -> SDL3 Audio
                                                                  -> A/V Sync
```

## UDP Protocol Design

**16-byte packet header:**
```
| Magic(1) | Version(1) | Type(1) | Flags(1) | Sequence(4) | Timestamp_us(4) | FrameID(2) | FragIdx(1) | FragTotal(1) |
```

**Packet types:** VIDEO_DATA, AUDIO_DATA, HELLO, WELCOME, ACK, NACK, KEYFRAME_REQ, PING, PONG, BYE, STREAM_CONFIG

**Reliability:** Keyframes reliably delivered (NACK + retransmit). P-frames and audio are best-effort.

**Connection flow:** Client HELLO -> Server WELCOME (stream config + SPS/PPS) -> Stream starts with IDR keyframe

## Project Structure
```
lancast/
├── CMakeLists.txt
├── cmake/
│   ├── FindFFmpeg.cmake
│   └── PlatformSetup.cmake
├── src/
│   ├── main.cpp                        # Entry point: --host / --client <ip>
│   ├── core/
│   │   ├── types.h                     # RawVideoFrame, RawAudioFrame, EncodedPacket, StreamConfig
│   │   ├── ring_buffer.h              # Lock-free SPSC ring buffer
│   │   ├── thread_safe_queue.h        # MPSC queue (mutex + condvar)
│   │   ├── clock.h                    # PTS generation, clock sync
│   │   ├── logger.h                   # Simple logging
│   │   └── logger.cpp
│   ├── capture/
│   │   ├── capture_source.h           # ICaptureSource interface
│   │   ├── screen_capture_x11.h       # X11 XShm capture header
│   │   ├── screen_capture_x11.cpp     # X11 XShm implementation
│   │   ├── audio_capture_pulse.h      # PulseAudio monitor capture
│   │   ├── audio_capture_pulse.cpp
│   │   └── capture_factory.cpp        # Platform factory
│   ├── encode/
│   │   ├── video_encoder.h
│   │   ├── video_encoder.cpp          # FFmpeg libx264 (+ optional VAAPI)
│   │   ├── audio_encoder.h
│   │   └── audio_encoder.cpp          # FFmpeg libopus
│   ├── decode/
│   │   ├── video_decoder.h
│   │   ├── video_decoder.cpp          # FFmpeg H.264 decoder
│   │   ├── audio_decoder.h
│   │   └── audio_decoder.cpp          # FFmpeg Opus decoder
│   ├── net/
│   │   ├── protocol.h                 # Packet header, message types, payloads
│   │   ├── socket.h
│   │   ├── socket.cpp                 # UDP socket wrapper (POSIX sockets)
│   │   ├── server.h
│   │   ├── server.cpp                 # Host: accepts clients, sends stream
│   │   ├── client.h
│   │   ├── client.cpp                 # Client: connects, receives stream
│   │   ├── packet_fragmenter.h
│   │   ├── packet_fragmenter.cpp      # Split frames into <=1184 byte chunks
│   │   ├── packet_assembler.h
│   │   └── packet_assembler.cpp       # Reassemble fragments into frames
│   ├── render/
│   │   ├── sdl_renderer.h
│   │   ├── sdl_renderer.cpp           # SDL3 YUV420p texture streaming
│   │   ├── audio_player.h
│   │   └── audio_player.cpp           # SDL3 audio playback
│   ├── sync/
│   │   ├── av_sync.h
│   │   └── av_sync.cpp                # Audio-master PTS synchronization
│   └── app/
│       ├── host_session.h
│       ├── host_session.cpp           # Orchestrates capture -> encode -> send
│       ├── client_session.h
│       ├── client_session.cpp         # Orchestrates recv -> decode -> render
│       ├── ui.h
│       └── ui.cpp                     # SDL3 host/join dialog
└── tests/
    ├── CMakeLists.txt
    ├── test_ring_buffer.cpp
    ├── test_protocol.cpp
    └── test_packet_roundtrip.cpp
```

## Key Design Patterns
- **RAII wrappers** for FFmpeg types (AVCodecContext, AVFrame, AVPacket with custom deleters)
- **Lock-free SPSC ring buffer** for capture->encode hot path
- **Thread-safe queues** (`mutex` + `condition_variable`) for other paths
- **Abstract interfaces** (`ICaptureSource`) for platform backends
- **C++20** standard (`std::jthread`, `std::atomic`, structured bindings)
- **Platform factory** to select capture backend at compile time

## Linux Screen Capture (X11 XShm)

The X11 XShm extension provides shared-memory screen capture:
1. Connect to X display via `XOpenDisplay`
2. Get root window geometry via `XGetWindowAttributes`
3. Create shared memory XImage via `XShmCreateImage` + `shmget`/`shmat`
4. Capture loop: `XShmGetImage(display, root_window, ximage, 0, 0, AllPlanes)` at target fps
5. Convert BGRA pixels to YUV420p (via `libswscale`) for encoding
6. Push `RawVideoFrame` into the encode queue

**Key details:**
- XShm avoids copying pixel data over the X protocol socket - shared memory is direct
- Capture runs on a dedicated thread with a timer (33ms for 30fps)
- Pixel format from X11 is typically BGRA32, needs `sws_scale` to NV12/I420

## Linux Audio Capture (PulseAudio)

PulseAudio monitor source captures system audio output:
1. Create PulseAudio context + mainloop
2. Get default sink name, append `.monitor` to get the monitor source
3. Create a recording stream from the monitor source (48kHz, stereo, float32)
4. Read callback provides PCM samples -> push into audio encode queue

## Video Encoding (libx264 via FFmpeg)

Using software H.264 for maximum compatibility:
- Encoder: `avcodec_find_encoder_by_name("libx264")`
- Preset: `"ultrafast"` for lowest encoding latency
- Tune: `"zerolatency"` (disables B-frames, reduces lookahead)
- Bitrate: 4-8 Mbps CBR for 1080p30 on LAN
- GOP: 60 frames (2 seconds), no B-frames
- Pixel format: `AV_PIX_FMT_YUV420P`
- Thread count: 4 (parallel encoding slices)
- On-demand IDR keyframe generation

## Implementation Phases

### Phase 1: Foundation + Network Test
1. CMake build system + dependencies (`apt install libavcodec-dev libsdl3-dev libx11-dev libxext-dev libpulse-dev`)
2. `core/` types, logger, ring buffer, thread-safe queue
3. `net/protocol.h` packet header structs
4. `net/socket.cpp` UDP wrapper (POSIX sockets)
5. `net/packet_fragmenter.cpp` + `net/packet_assembler.cpp` + unit tests
6. Basic server/client that sends synthetic test data over UDP
- **Result:** `lancast --host` and `lancast --client <ip>` exchange test data on LAN

### Phase 2: Screen Capture + Raw Display
1. `capture/screen_capture_x11.cpp` with XShm
2. `render/sdl_renderer.cpp` with SDL3
3. Connect: capture -> fragment -> send -> assemble -> display (raw pixels, ~10fps)
4. Connection handshake (HELLO/WELCOME)
- **Result:** Working screen share (low fps, uncompressed)

### Phase 3: H.264 Encoding/Decoding
1. `encode/video_encoder.cpp` with libx264 ultrafast/zerolatency
2. `decode/video_decoder.cpp`
3. STREAM_CONFIG packet for SPS/PPS
4. Insert encoder/decoder into pipeline
5. Keyframe request mechanism
- **Result:** 30fps screen sharing with ~50-80ms latency

### Phase 4: Audio
1. `capture/audio_capture_pulse.cpp` with PulseAudio monitor
2. `encode/audio_encoder.cpp` (Opus) + `decode/audio_decoder.cpp`
3. `render/audio_player.cpp` (SDL3 audio)
4. AUDIO_DATA packet handling
5. `sync/av_sync.cpp` - audio-master synchronization
- **Result:** Screen + audio streaming with lip sync

### Phase 5: Polish
1. Connection state machine, graceful disconnect (BYE)
2. NACK retransmission for keyframes
3. PING/PONG RTT measurement
4. Simple adaptive bitrate
5. UI: host/join screen with IP entry field
6. Fullscreen toggle (F11), window resize
7. CLI options: `--port`, `--bitrate`, `--fps`, `--resolution`

### Phase 6: Windows Port (later)
1. `capture/screen_capture_dxgi.cpp` - DXGI Desktop Duplication
2. `capture/audio_capture_wasapi.cpp` - WASAPI loopback
3. `net/socket.cpp` - add Winsock support (`#ifdef _WIN32`)
4. Optional: `h264_nvenc` / `h264_amf` / `h264_qsv` HW encoding
5. CMake: add MSVC/MinGW build support

### Phase 7: macOS Port (later)
1. `capture/screen_capture_mac.mm` - ScreenCaptureKit
2. `h264_videotoolbox` HW encoding
3. macOS framework linking

## Performance Targets (1080p30 on Gigabit LAN)
| Stage | Latency |
|-------|---------|
| Screen capture (XShm) | 0-33ms |
| SW encoding (libx264 ultrafast) | 8-15ms |
| Network (LAN) | <1ms |
| SW decoding | 3-8ms |
| Rendering | <2ms |
| **Total** | **~40-60ms** |

Note: Software encoding is slower than HW but still fast enough for LAN streaming. Optional VAAPI can bring encoding down to 3-8ms.

## Dependencies (Linux)
| Library | Install | Purpose |
|---------|---------|---------|
| FFmpeg 6.x/7.x | `apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev` | Encode/decode |
| SDL3 | FetchContent or build from source | Render + audio |
| libX11 + libXext | `apt install libx11-dev libxext-dev` | X11 screen capture |
| libpulse | `apt install libpulse-dev` | PulseAudio audio capture |
| GoogleTest 1.14+ | FetchContent | Testing |

## Verification
1. Build: `cmake -B build && cmake --build build`
2. Host: `./lancast --host` (starts server on port 7878)
3. Client: `./lancast --client 192.168.x.x` (connects and displays stream)
4. Verify: screen content visible on client with audio, <100ms perceived latency
5. Tests: `ctest --test-dir build`
