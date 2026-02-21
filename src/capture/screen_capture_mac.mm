#include "capture/screen_capture_mac.h"
#include "core/logger.h"
#include "core/clock.h"

// FFmpeg headers must come before ScreenCaptureKit to avoid AVMediaType
// name collision (macOS AVFoundation defines AVMediaType as NSString*,
// FFmpeg defines it as an enum).
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

// Now include Objective-C frameworks — AVFoundation's AVMediaType typedef
// will see the FFmpeg enum already defined and we use the workaround below.
#ifdef __OBJC__
// Prevent AVFoundation AVMediaType typedef from conflicting with FFmpeg's enum
#define AVMediaType AVMediaType_ObjC
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#undef AVMediaType
#endif

namespace lancast {

static constexpr const char* TAG = "ScreenCaptureMac";

// ──────────────────────────────────────────────────────────────
// Objective-C delegate that receives CMSampleBuffers from SCStream
// ──────────────────────────────────────────────────────────────

} // namespace lancast (pause for ObjC)

@interface LancastStreamDelegate : NSObject <SCStreamOutput>
@property (nonatomic, assign) lancast::SCStreamManager* manager;
@property (nonatomic, assign) uint32_t targetWidth;
@property (nonatomic, assign) uint32_t targetHeight;
@end

@implementation LancastStreamDelegate {
    SwsContext* _swsCtx;
    uint32_t _lastSrcWidth;
    uint32_t _lastSrcHeight;
}

- (instancetype)initWithManager:(lancast::SCStreamManager*)manager
                    targetWidth:(uint32_t)tw
                   targetHeight:(uint32_t)th {
    self = [super init];
    if (self) {
        _manager = manager;
        _targetWidth = tw;
        _targetHeight = th;
        _swsCtx = nullptr;
        _lastSrcWidth = 0;
        _lastSrcHeight = 0;
    }
    return self;
}

- (void)cleanup {
    if (_swsCtx) {
        sws_freeContext(_swsCtx);
        _swsCtx = nullptr;
    }
}

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    if (!_manager) return;

    if (type == SCStreamOutputTypeScreen) {
        [self handleVideoBuffer:sampleBuffer];
    } else if (type == SCStreamOutputTypeAudio) {
        [self handleAudioBuffer:sampleBuffer];
    }
}

- (void)handleVideoBuffer:(CMSampleBufferRef)sampleBuffer {
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    size_t srcWidth = CVPixelBufferGetWidth(imageBuffer);
    size_t srcHeight = CVPixelBufferGetHeight(imageBuffer);
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(imageBuffer);

    // ScreenCaptureKit typically delivers BGRA
    AVPixelFormat srcFmt;
    if (pixelFormat == kCVPixelFormatType_32BGRA) {
        srcFmt = AV_PIX_FMT_BGRA;
    } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
               pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        srcFmt = AV_PIX_FMT_NV12;
    } else {
        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        LOG_WARN("ScreenCaptureMac", "Unsupported pixel format: %u", (unsigned)pixelFormat);
        return;
    }

    // Ensure even dimensions for YUV420P
    uint32_t dstW = _targetWidth > 0 ? _targetWidth : (uint32_t)(srcWidth & ~1u);
    uint32_t dstH = _targetHeight > 0 ? _targetHeight : (uint32_t)(srcHeight & ~1u);
    dstW &= ~1u;
    dstH &= ~1u;

    // Recreate swscale context if source dimensions changed
    if (_swsCtx == nullptr || _lastSrcWidth != srcWidth || _lastSrcHeight != srcHeight) {
        if (_swsCtx) sws_freeContext(_swsCtx);
        _swsCtx = sws_getContext(
            (int)srcWidth, (int)srcHeight, srcFmt,
            (int)dstW, (int)dstH, AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        _lastSrcWidth = (uint32_t)srcWidth;
        _lastSrcHeight = (uint32_t)srcHeight;
        if (!_swsCtx) {
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            LOG_ERROR("ScreenCaptureMac", "Failed to create swscale context");
            return;
        }
    }

    // Build source slice pointers
    const uint8_t* srcData[4] = {};
    int srcLinesize[4] = {};

    if (srcFmt == AV_PIX_FMT_BGRA) {
        srcData[0] = (const uint8_t*)CVPixelBufferGetBaseAddress(imageBuffer);
        srcLinesize[0] = (int)CVPixelBufferGetBytesPerRow(imageBuffer);
    } else {
        // NV12: two planes
        srcData[0] = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0);
        srcData[1] = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1);
        srcLinesize[0] = (int)CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
        srcLinesize[1] = (int)CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);
    }

    // Allocate destination YUV420P buffer
    int ySize = (int)dstW * (int)dstH;
    int uvSize = ySize / 4;
    lancast::RawVideoFrame frame;
    frame.width = dstW;
    frame.height = dstH;
    frame.data.resize(ySize + 2 * uvSize);

    uint8_t* dstData[4] = {
        frame.data.data(),
        frame.data.data() + ySize,
        frame.data.data() + ySize + uvSize,
        nullptr
    };
    int dstLinesize[4] = {
        (int)dstW,
        (int)dstW / 2,
        (int)dstW / 2,
        0
    };

    sws_scale(_swsCtx,
              srcData, srcLinesize, 0, (int)srcHeight,
              dstData, dstLinesize);

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    lancast::Clock clock;
    frame.pts_us = clock.now_us();
    _manager->push_video_frame(std::move(frame));
}

- (void)handleAudioBuffer:(CMSampleBufferRef)sampleBuffer {
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) return;

    size_t totalBytes = CMBlockBufferGetDataLength(blockBuffer);
    if (totalBytes == 0) return;

    // Get audio format
    CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    const AudioStreamBasicDescription* asbd =
        CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (!asbd) return;

    uint32_t sampleRate = (uint32_t)asbd->mSampleRate;
    uint16_t channels = (uint16_t)asbd->mChannelsPerFrame;

    // ScreenCaptureKit delivers float32 interleaved PCM
    char* dataPtr = nullptr;
    size_t lengthOut = 0;
    OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &lengthOut, &dataPtr);
    if (status != noErr || !dataPtr) return;

    uint32_t numSamples = (uint32_t)(lengthOut / (sizeof(float) * channels));
    _manager->push_audio_samples((const float*)dataPtr, numSamples, sampleRate, channels);
}

@end

// ──────────────────────────────────────────────────────────────
// SCStreamManager implementation
// ──────────────────────────────────────────────────────────────

namespace lancast {

SCStreamManager::~SCStreamManager() {
    stop();
}

bool SCStreamManager::start(uint32_t target_width, uint32_t target_height,
                            uint64_t window_id) {
    if (running_) return true;

    __block bool success = false;
    __block uint32_t natW = 0, natH = 0;
    __block SCContentFilter* captureFilter = nil;

    // Get shareable content synchronously via semaphore
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentWithCompletionHandler:
        ^(SCShareableContent* content, NSError* error) {
            if (error || !content) {
                LOG_ERROR(TAG, "Failed to get shareable content: %s",
                          error ? [[error localizedDescription] UTF8String] : "unknown");
                dispatch_semaphore_signal(sem);
                return;
            }

            if (window_id != 0) {
                // Find the specific window by CGWindowID
                for (SCWindow* window in content.windows) {
                    if (window.windowID == (CGWindowID)window_id) {
                        captureFilter = [[SCContentFilter alloc]
                            initWithDesktopIndependentWindow:window];
                        natW = (uint32_t)window.frame.size.width;
                        natH = (uint32_t)window.frame.size.height;
                        success = true;
                        break;
                    }
                }
                if (!success) {
                    LOG_ERROR(TAG, "Window 0x%llx not found", (unsigned long long)window_id);
                }
            } else {
                // Capture the main display
                SCDisplay* mainDisplay = content.displays.firstObject;
                if (mainDisplay) {
                    captureFilter = [[SCContentFilter alloc]
                        initWithDisplay:mainDisplay excludingWindows:@[]];
                    natW = (uint32_t)mainDisplay.width;
                    natH = (uint32_t)mainDisplay.height;
                    success = true;
                }
            }

            dispatch_semaphore_signal(sem);
        }];

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

    if (!success || !captureFilter) return false;

    native_width_ = natW;
    native_height_ = natH;

    uint32_t outW = target_width > 0 ? target_width : natW;
    uint32_t outH = target_height > 0 ? target_height : natH;
    outW &= ~1u;
    outH &= ~1u;

    // Configure the stream
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.width = outW;
    config.height = outH;
    config.minimumFrameInterval = CMTimeMake(1, 60); // Up to 60fps
    config.queueDepth = 4;
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.showsCursor = YES;

    // Enable audio capture
    config.capturesAudio = YES;
    config.sampleRate = 48000;
    config.channelCount = 2;

    audio_channels_ = 2;

    filter_ = captureFilter;
    delegate_ = [[LancastStreamDelegate alloc] initWithManager:this
                                                   targetWidth:outW
                                                  targetHeight:outH];

    stream_ = [[SCStream alloc] initWithFilter:captureFilter
                                 configuration:config
                                      delegate:nil];

    NSError* addOutputError = nil;

    // Add video output
    [stream_ addStreamOutput:delegate_
                        type:SCStreamOutputTypeScreen
          sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0)
                       error:&addOutputError];
    if (addOutputError) {
        LOG_ERROR(TAG, "Failed to add video output: %s",
                  [[addOutputError localizedDescription] UTF8String]);
        return false;
    }

    // Add audio output
    addOutputError = nil;
    [stream_ addStreamOutput:delegate_
                        type:SCStreamOutputTypeAudio
          sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0)
                       error:&addOutputError];
    if (addOutputError) {
        LOG_ERROR(TAG, "Failed to add audio output: %s",
                  [[addOutputError localizedDescription] UTF8String]);
        return false;
    }

    // Start the stream
    __block bool startOk = false;
    dispatch_semaphore_t startSem = dispatch_semaphore_create(0);

    [stream_ startCaptureWithCompletionHandler:^(NSError* error) {
        if (error) {
            LOG_ERROR(TAG, "Failed to start capture: %s",
                      [[error localizedDescription] UTF8String]);
        } else {
            startOk = true;
        }
        dispatch_semaphore_signal(startSem);
    }];

    dispatch_semaphore_wait(startSem, DISPATCH_TIME_FOREVER);

    if (!startOk) return false;

    running_ = true;
    LOG_INFO(TAG, "SCStream started: native %ux%u, output %ux%u, audio 48kHz stereo",
             natW, natH, outW, outH);
    return true;
}

void SCStreamManager::stop() {
    if (!running_) return;
    running_ = false;

    if (stream_) {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [stream_ stopCaptureWithCompletionHandler:^(NSError* error) {
            if (error) {
                LOG_WARN(TAG, "Error stopping capture: %s",
                         [[error localizedDescription] UTF8String]);
            }
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        stream_ = nil;
    }

    if (delegate_) {
        [(LancastStreamDelegate*)delegate_ cleanup];
        [(LancastStreamDelegate*)delegate_ setManager:nullptr];
        delegate_ = nil;
    }

    filter_ = nil;

    LOG_INFO(TAG, "SCStream stopped");
}

void SCStreamManager::push_video_frame(RawVideoFrame frame) {
    std::lock_guard lock(video_mutex_);
    if (video_queue_.size() >= kMaxVideoFrames) {
        video_queue_.pop(); // drop oldest
    }
    video_queue_.push(std::move(frame));
    video_cv_.notify_one();
}

void SCStreamManager::push_audio_samples(const float* samples, uint32_t num_samples,
                                          uint32_t /*sample_rate*/, uint16_t channels) {
    std::lock_guard lock(audio_mutex_);
    size_t count = static_cast<size_t>(num_samples) * channels;
    audio_buffer_.insert(audio_buffer_.end(), samples, samples + count);
    audio_channels_ = channels;
    audio_cv_.notify_one();
}

std::optional<RawVideoFrame> SCStreamManager::pop_video_frame() {
    std::unique_lock lock(video_mutex_);
    if (video_queue_.empty()) {
        video_cv_.wait_for(lock, std::chrono::milliseconds(50),
                           [this] { return !video_queue_.empty() || !running_; });
    }
    if (video_queue_.empty()) return std::nullopt;
    auto frame = std::move(video_queue_.front());
    video_queue_.pop();
    return frame;
}

std::vector<float> SCStreamManager::pop_audio_samples(uint32_t num_samples, uint16_t channels) {
    size_t needed = static_cast<size_t>(num_samples) * channels;
    std::unique_lock lock(audio_mutex_);
    if (audio_buffer_.size() < needed) {
        audio_cv_.wait_for(lock, std::chrono::milliseconds(50),
                           [&] { return audio_buffer_.size() >= needed || !running_; });
    }
    if (audio_buffer_.size() < needed) return {};
    std::vector<float> result(audio_buffer_.begin(), audio_buffer_.begin() + needed);
    audio_buffer_.erase(audio_buffer_.begin(), audio_buffer_.begin() + needed);
    return result;
}

// ──────────────────────────────────────────────────────────────
// ScreenCaptureMac implementation
// ──────────────────────────────────────────────────────────────

ScreenCaptureMac::~ScreenCaptureMac() {
    shutdown();
}

bool ScreenCaptureMac::init(uint32_t target_width, uint32_t target_height,
                             uint64_t window_id) {
    manager_ = std::make_shared<SCStreamManager>();
    if (!manager_->start(target_width, target_height, window_id)) {
        LOG_ERROR(TAG, "Failed to start SCStreamManager");
        manager_.reset();
        return false;
    }

    native_width_ = manager_->native_width();
    native_height_ = manager_->native_height();
    target_width_ = target_width > 0 ? (target_width & ~1u) : (native_width_ & ~1u);
    target_height_ = target_height > 0 ? (target_height & ~1u) : (native_height_ & ~1u);

    initialized_ = true;
    LOG_INFO(TAG, "macOS screen capture initialized: %ux%u -> %ux%u",
             native_width_, native_height_, target_width_, target_height_);
    return true;
}

std::optional<RawVideoFrame> ScreenCaptureMac::capture_frame() {
    if (!initialized_ || !manager_) return std::nullopt;
    return manager_->pop_video_frame();
}

void ScreenCaptureMac::shutdown() {
    if (!initialized_) return;

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    // Manager is shared — only stop if we're the last reference
    if (manager_ && manager_.use_count() <= 1) {
        manager_->stop();
    }
    manager_.reset();

    initialized_ = false;
    LOG_INFO(TAG, "macOS screen capture shut down");
}

std::vector<WindowInfo> ScreenCaptureMac::list_windows() {
    __block std::vector<WindowInfo> result;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                              onScreenWindowsOnly:YES
                                                completionHandler:
        ^(SCShareableContent* content, NSError* error) {
            if (error || !content) {
                dispatch_semaphore_signal(sem);
                return;
            }

            for (SCWindow* window in content.windows) {
                // Skip windows with no title or zero size
                if (!window.title || window.title.length == 0) continue;
                if (window.frame.size.width < 1 || window.frame.size.height < 1) continue;

                WindowInfo info;
                info.id = window.windowID;
                info.title = [window.title UTF8String];
                info.width = (uint32_t)window.frame.size.width;
                info.height = (uint32_t)window.frame.size.height;
                result.push_back(std::move(info));
            }

            dispatch_semaphore_signal(sem);
        }];

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    return result;
}

} // namespace lancast
