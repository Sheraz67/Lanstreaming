# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commit Rules

- Never include `Co-Authored-By` lines in commits.

## Project Overview

**lancast** is a native C++ desktop application for streaming screen and audio over LAN networks. Manual IP entry (no auto-discovery). Supports Linux (X11 + PulseAudio) and macOS (ScreenCaptureKit, macOS 12.3+). Windows is planned later. The full specification lives in `lancast-plan.md`.

## Build & Run

```bash
# Install dependencies (Linux)
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libx11-dev libxext-dev libpulse-dev

# macOS: no manual dependency install needed — build-dmg.sh handles everything

# Configure and build
cmake -B build && cmake --build build

# Run (three modes)
./build/lancast                              # Launch SDL3 UI (host/join dialog)
./build/lancast --host                       # Start as server on port 7878
./build/lancast --client 192.168.x.x         # Connect to host

# Host options
./build/lancast --host --port 9000 --fps 60 --bitrate 8000000 --resolution 1920x1080
./build/lancast --host --window 0x1234567    # Capture specific window
./build/lancast --list-windows               # List capturable windows
./build/lancast -v                           # Verbose/debug logging

# Run all tests
ctest --test-dir build

# Run a single test by name
ctest --test-dir build -R test_ring_buffer

# Build with verbose output (useful for debugging compile errors)
cmake --build build --verbose

# Build AppImage (Linux)
bash packaging/build-appimage.sh

# Build DMG (macOS — auto-installs Homebrew + FFmpeg if needed)
bash packaging/build-dmg.sh
# Output: Lancast-0.1.0-macOS.dmg (fully self-contained, no deps for end user)
```

SDL3 and GoogleTest 1.15+ are fetched via CMake FetchContent. Requires CMake 3.21+ and C++20.

## Architecture

All code is in the `lancast` namespace.

### Host Pipeline (6 threads)
```
Linux:  X11 XShm capture   -> [RingBuffer] -> VideoEncoder(H.264) -> [RingBuffer] -> UDP Server
        PulseAudio monitor  -> [TSQueue]    -> AudioEncoder(Opus)  -> [TSQueue]   ----^

macOS:  ScreenCaptureKit    -> [RingBuffer] -> VideoEncoder(H.264) -> [RingBuffer] -> UDP Server
        (shared SCStream)   -> [TSQueue]    -> AudioEncoder(Opus)  -> [TSQueue]   ----^
                                                                  Server poll thread (NACK/keyframe requests)
```

### Client Pipeline (3 threads + main thread SDL render loop + SDL audio callback)
```
UDP recv -> PacketAssembler -> [TSQueue] -> VideoDecoder -> [TSQueue] -> SDL3 Renderer (main thread)
                            -> [TSQueue] -> AudioDecoder -> SDL3 Audio callback
```

### CMake Library Targets

When adding new source files, add them to the appropriate static library target in `CMakeLists.txt`:

| Target | Sources | Links to |
|--------|---------|----------|
| `lancast_core` | `core/logger.cpp` + headers | FFmpeg libs |
| `lancast_net` | `net/socket, packet_fragmenter, packet_assembler, server, client` | lancast_core |
| `lancast_capture` | Linux: `capture/screen_capture_x11`, macOS: `capture/screen_capture_mac.mm` | lancast_core, X11/ScreenCaptureKit, swscale |
| `lancast_audio_capture` | Linux: `capture/audio_capture_pulse`, macOS: `capture/audio_capture_mac.mm` | lancast_core, PulseAudio/lancast_capture |
| `lancast_encode` | `encode/video_encoder, audio_encoder` | lancast_core, avcodec, swresample |
| `lancast_decode` | `decode/video_decoder, audio_decoder` | lancast_core, avcodec, swresample |
| `lancast_render` | `render/sdl_renderer` | lancast_core, SDL3 |
| `lancast_audio_render` | `render/audio_player` | lancast_core, SDL3 |
| `lancast_app` | `app/host_session, client_session, launcher_ui` | all above |

Tests in `tests/CMakeLists.txt` link to the relevant library + `GTest::gtest_main`.

### Key Modules

- **`core/`** — Shared types (`RawVideoFrame`, `RawAudioFrame`, `EncodedPacket`, `StreamConfig` in `types.h`), lock-free SPSC `RingBuffer` (capture->encode hot path), `ThreadSafeQueue` (mutex+condvar MPSC), logger, clock/PTS. RAII smart pointers for FFmpeg types in `ffmpeg_ptrs.h` (`AVCodecContextPtr`, `AVFramePtr`, `AVPacketPtr`, `SwrContextPtr` with factory functions `make_codec_context()`, `make_frame()`, `make_packet()`, `make_swr_context()`)
- **`capture/`** — `ICaptureSource` interface (`capture_source.h`) and `IAudioCapture` interface (`audio_capture.h`) with platform backends. Linux: X11 XShm captures BGRA32 via shared memory + PulseAudio monitor for audio. macOS: ScreenCaptureKit (`screen_capture_mac.mm`, `audio_capture_mac.mm`) with shared `SCStreamManager` for combined screen+audio capture. Both convert to YUV420p via `sws_scale`. Supports per-window capture via window ID.
- **`encode/`** — FFmpeg libx264 (`ultrafast`/`zerolatency`, default 6 Mbps CBR, GOP 60, no B-frames, 4 threads) and libopus encoder
- **`decode/`** — FFmpeg H.264 and Opus decoders
- **`net/`** — Custom UDP protocol (version 2). 16-byte packed header: `Magic(1) | Version(1) | Type(1) | Flags(1) | Sequence(2) | Timestamp_us(4) | FrameID(2) | FragIdx(2) | FragTotal(2)`. Packet fragmenter splits frames into <=1184 byte chunks. Keyframes are NACK-reliable; P-frames and audio are best-effort. Connection flow: HELLO -> WELCOME (with stream config) -> STREAM_CONFIG (SPS/PPS) -> IDR keyframe
- **`render/`** — SDL3 YUV420p texture streaming and SDL3 audio playback
- **`app/`** — `HostSession` and `ClientSession` orchestrators, `LauncherUI` SDL3 host/join dialog with window picker

### Gotchas

- X11's `X.h` defines `None` as `0L`, conflicting with `LaunchMode::None`. Use `#undef None` after including X11 headers when needed (Linux only).
- FFmpeg headers must be included inside `extern "C" {}` blocks.
- Platform-specific compile definitions (`LANCAST_PLATFORM_LINUX`, `LANCAST_PLATFORM_MACOS`, etc.) are set in `cmake/PlatformSetup.cmake`.
- macOS ScreenCaptureKit requires macOS 12.3+. Screen recording permission must be granted in System Settings > Privacy & Security > Screen Recording.
- macOS capture uses a shared `SCStreamManager` — `ScreenCaptureMac` owns it, `AudioCaptureMac` holds a reference. Both video and audio come from the same `SCStream`.

## Implementation Phases

The project follows 7 phases defined in `lancast-plan.md`. Phases 1-5 are Linux, Phase 6 is Windows, Phase 7 is macOS. Each phase builds on the previous. Check `lancast-plan.md` for detailed per-phase deliverables.

## Performance Target

40-60ms total latency for 1080p30 on Gigabit LAN (capture 0-33ms + SW encode 8-15ms + network <1ms + decode 3-8ms + render <2ms).
