# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**lancast** is a native C++ desktop application for streaming screen and audio over LAN networks. Manual IP entry (no auto-discovery). Linux (X11) is the primary platform; Windows and macOS are planned later. The full specification lives in `lancast-plan.md`.

## Build & Run

```bash
# Install dependencies (Linux)
apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libx11-dev libxext-dev libpulse-dev

# Configure and build
cmake -B build && cmake --build build

# Run
./build/lancast --host                    # Start as server on port 7878
./build/lancast --client 192.168.x.x     # Connect to host

# Run all tests
ctest --test-dir build

# Run a single test by name
ctest --test-dir build -R test_ring_buffer

# Build with verbose output (useful for debugging compile errors)
cmake --build build --verbose
```

SDL3 and GoogleTest 1.14+ are fetched via CMake FetchContent. Requires CMake 3.21+ and C++20.

## Architecture

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

### Key Modules

- **`core/`** — Shared types (`RawVideoFrame`, `EncodedPacket`, `StreamConfig`), lock-free SPSC ring buffer (capture->encode hot path), thread-safe MPSC queue (mutex+condvar), logger, clock/PTS
- **`capture/`** — `ICaptureSource` interface with platform backends. X11 XShm captures BGRA32 via shared memory, converted to YUV420p via `sws_scale`. PulseAudio monitor captures system audio at 48kHz stereo float32
- **`encode/`** — FFmpeg libx264 (`ultrafast`/`zerolatency`, 4-8 Mbps CBR, GOP 60, no B-frames, 4 threads) and libopus encoder
- **`decode/`** — FFmpeg H.264 and Opus decoders
- **`net/`** — Custom 16-byte UDP packet header protocol. Packet fragmenter splits frames into <=1184 byte chunks. Keyframes are NACK-reliable; P-frames and audio are best-effort. Connection flow: HELLO -> WELCOME (with SPS/PPS) -> IDR keyframe
- **`render/`** — SDL3 YUV420p texture streaming and SDL3 audio playback
- **`sync/`** — Audio-master A/V synchronization
- **`app/`** — `HostSession` and `ClientSession` orchestrators, SDL3 UI (host/join dialog)

### Design Patterns

- RAII wrappers with custom deleters for FFmpeg types (AVCodecContext, AVFrame, AVPacket)
- Lock-free SPSC ring buffer for the capture->encode hot path; mutex+condvar MPSC queues elsewhere
- `ICaptureSource` abstract interface + platform factory for compile-time backend selection
- C++20: `std::jthread`, `std::atomic`, structured bindings

## Implementation Phases

The project follows 7 phases defined in `lancast-plan.md`. Phases 1-5 are Linux, Phase 6 is Windows, Phase 7 is macOS. Each phase builds on the previous — start with Phase 1 (CMake + core types + UDP protocol + fragmenter/assembler + tests) and work sequentially. Check `lancast-plan.md` for detailed per-phase deliverables.

## Performance Target

40-60ms total latency for 1080p30 on Gigabit LAN (capture 0-33ms + SW encode 8-15ms + network <1ms + decode 3-8ms + render <2ms).
