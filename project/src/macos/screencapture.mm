#include "../include/screencapture.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreGraphics/CoreGraphics.h>
#include <iostream>
#include <chrono>
#import <Foundation/Foundation.h>

void disable_app_nap() {
    if ([[NSProcessInfo processInfo] respondsToSelector:@selector(beginActivityWithOptions:reason:)]) {
        // NSActivityUserInitiatedAllowingIdleSystemSleep:
        // Keeps the CPU active and prevents the app from being throttled
        id activity = [[NSProcessInfo processInfo] 
            beginActivityWithOptions:0x000000FF | 0x00000100ULL 
            reason:@"Latency Critical Macro Engine"];
        
        // We "leak" the activity object intentionally to keep it active for the 
        // lifetime of the process.
        CFRetain((__bridge CFTypeRef)activity);
        std::cout << "[Power] App Nap has been disabled." << std::endl;
    }
}

@interface SimpleCaptureDelegate : NSObject <SCStreamOutput, SCStreamDelegate>
@property (atomic, assign) uint64_t frameCount;
@property (atomic, assign) double currentFPS;
@property (nonatomic, assign) std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;
@property (nonatomic, assign) FrameCallback callback;
@property (nonatomic, assign) CVPixelBufferRef latestBuffer;
@property (nonatomic, assign) std::mutex* bufferMutex;
@property (nonatomic, assign) std::condition_variable* bufferCV;
@property (nonatomic, assign) bool hasNewFrame;
@property (nonatomic, assign) bool shouldCleanup;  // Flag to control cleanup
@end

@implementation SimpleCaptureDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _frameCount = 0;
        _currentFPS = 0.0;
        _lastFrameTime = std::chrono::high_resolution_clock::now();
        _latestBuffer = nil;
        _hasNewFrame = false;
        _shouldCleanup = true;
    }
    return self;
}

- (void)dealloc {
    if (_shouldCleanup && _latestBuffer) {
        CVPixelBufferRelease(_latestBuffer);
        _latestBuffer = nil;
    }
}

- (void)stream:(SCStream *)stream 
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
    ofType:(SCStreamOutputType)type {
    
    if (type != SCStreamOutputTypeScreen) return;
    
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;
    
    // Calculate FPS
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastFrameTime).count();
    if (duration > 0) {
        double instantFPS = 1000.0 / duration;
        _currentFPS = (_currentFPS * 0.9) + (instantFPS * 0.1);
    }
    _lastFrameTime = now;
    _frameCount++;
    
    // Call callback if set (BEFORE storing buffer)
    if (_callback) {
        _callback(pixelBuffer);
    }
    
    // Store latest frame for blocking API
    if (_bufferMutex) {
        std::lock_guard<std::mutex> lock(*_bufferMutex);
        
        // Release old buffer
        if (_latestBuffer) {
            CVPixelBufferRelease(_latestBuffer);
            _latestBuffer = nil;
        }
        
        // Retain new buffer
        _latestBuffer = CVPixelBufferRetain(pixelBuffer);
        _hasNewFrame = true;
        
        if (_bufferCV) {
            _bufferCV->notify_one();
        }
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    if (error) {
        std::cerr << "Stream error: " << error.localizedDescription.UTF8String << std::endl;
    }
}

@end

// Simplified Pimpl - with blocking API support
class ScreenCapturer::Impl {
public:
    SCStream *stream;
    SimpleCaptureDelegate *delegate;
    dispatch_queue_t queue;
    bool running;
    std::mutex bufferMutex;
    std::condition_variable bufferCV;
    CVPixelBufferRef lockedBuffer;  // Track currently locked buffer
    
    Impl() : stream(nil), delegate(nil), queue(nil), running(false), lockedBuffer(nil) {}
    
    ~Impl() {
        // Stop stream first
        stop();
        
        // Unlock any remaining locked buffer AFTER stopping
        if (lockedBuffer) {
            CVPixelBufferUnlockBaseAddress(lockedBuffer, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(lockedBuffer);
            lockedBuffer = nil;
        }
    }
    
    bool start(FrameCallback callback, int target_fps, CGRect region = CGRectNull, CGDirectDisplayID display_id = kCGNullDirectDisplay) {
        if (running) return false;
        if (target_fps <= 0) target_fps = 60;
        if (display_id == kCGNullDirectDisplay) {
            display_id = CGMainDisplayID();
        }
        
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block bool success = false;
        
        [SCShareableContent getShareableContentWithCompletionHandler:^(
            SCShareableContent *content, NSError *error) {
            
            if (error) {
                std::cerr << "Error getting content: " << error.localizedDescription.UTF8String << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            if (content.displays.count == 0) {
                std::cerr << "No displays found" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            //SCDisplay *display = content.displays.firstObject;
            
            SCDisplay *display = nil;
            for (SCDisplay *d in content.displays) {
                if (d.displayID == display_id) {
                    display = d;
                    break;
                }
            }

            if(!display) {
                std::cerr << "Specified display not found" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }

            pid_t currentPID = [NSProcessInfo processInfo].processIdentifier;

            SCRunningApplication *currentApp = nil;
            for (SCRunningApplication *app in content.applications) {
                if (app.processID == currentPID) {
                    currentApp = app;
                    break;
                }
            }

            SCContentFilter *filter = [[SCContentFilter alloc]
                initWithDisplay:display
                excludingApplications:currentApp ? @[currentApp] : @[]
                exceptingWindows:@[]];
            
            SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
            
            // Check if region is specified
            bool useRegion = !CGRectIsNull(region);
            
            if (useRegion) {
                config.sourceRect = region;
                config.width = region.size.width;
                config.height = region.size.height;
            } else {
                config.width = display.width;
                config.height = display.height;
            }
            
            config.minimumFrameInterval = CMTimeMake(1, target_fps);
            config.queueDepth = 8;  // Lower for macOS 15
            config.pixelFormat = kCVPixelFormatType_32BGRA;
            config.showsCursor = NO;  // Cursor disabled
            config.shouldBeOpaque = YES;
            
            // macOS 15: Request high priority
            if (@available(macOS 15.0, *)) {
                config.captureResolution = SCCaptureResolutionAutomatic;
                // Try to hint we need real-time performance
                config.minimumFrameInterval = CMTimeMake(1, target_fps);
            }
            
            // Disable expensive features
            config.scalesToFit = NO;
            config.preservesAspectRatio = YES;
            
            // Shadow optimizations
            if (@available(macOS 13.0, *)) {
                config.ignoreShadowsSingleWindow = YES;
            }
            if (@available(macOS 14.2, *)) {
                config.ignoreShadowsDisplay = YES;
            }
            if (@available(macOS 14.2, *)) {
                config.includeChildWindows = NO;
            }
            
            std::cout << "Config: " << config.width << "x" << config.height 
                      << " @ " << target_fps << " FPS, queueDepth=" << config.queueDepth << std::endl;
            
            stream = [[SCStream alloc] initWithFilter:filter 
                                        configuration:config 
                                             delegate:nil];
            
            if (!stream) {
                std::cerr << "Failed to create stream" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            delegate = [[SimpleCaptureDelegate alloc] init];
            delegate.callback = callback;
            delegate.bufferMutex = &bufferMutex;
            delegate.bufferCV = &bufferCV;
            
            queue = dispatch_queue_create(
                "com.screencapture.simple",
                dispatch_queue_attr_make_with_qos_class(
                    DISPATCH_QUEUE_SERIAL,
                    QOS_CLASS_USER_INTERACTIVE, 0));  // Highest priority
            
            // macOS 15: Set real-time priority
            dispatch_set_target_queue(queue, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0));
            
            NSError *outputError = nil;
            [stream addStreamOutput:delegate 
                               type:SCStreamOutputTypeScreen 
                   sampleHandlerQueue:queue 
                              error:&outputError];
            
            if (outputError) {
                std::cerr << "Error adding output: " << outputError.localizedDescription.UTF8String << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            
            [stream startCaptureWithCompletionHandler:^(NSError *startError) {
                if (startError) {
                    std::cerr << "Error starting: " << startError.localizedDescription.UTF8String << std::endl;
                } else {
                    std::cout << "Stream started successfully" << std::endl;
                }
                success = !startError;
                running = success;
                dispatch_semaphore_signal(semaphore);
            }];
        }];
        
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        return success;
    }
    
    void stop() {
        if (!running || !stream) return;
        
        running = false;  // Set this FIRST to stop other threads
        
        // Wake up any waiting threads
        bufferCV.notify_all();
        
        // Stop delegate from cleaning up (we'll do it manually)
        if (delegate) {
            delegate.shouldCleanup = false;
        }
        
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        [stream stopCaptureWithCompletionHandler:^(NSError *error) {
            dispatch_semaphore_signal(semaphore);
        }];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        
        // Manually clean up delegate's buffer
        if (delegate && delegate.latestBuffer) {
            CVPixelBufferRelease(delegate.latestBuffer);
            delegate.latestBuffer = nil;
        }
    }
};

// ScreenCapturer public API implementation
ScreenCapturer::ScreenCapturer() : impl(new Impl()) {}

ScreenCapturer::~ScreenCapturer() {
    delete impl;
}

bool ScreenCapturer::start(int target_fps, CGDirectDisplayID display_id) {
    return impl->start(nullptr, target_fps, CGRectNull, display_id);
}

bool ScreenCapturer::start(FrameCallback callback, int target_fps, CGDirectDisplayID display_id) {
    return impl->start(callback, target_fps, CGRectNull, display_id);
}

bool ScreenCapturer::start_region(CGRect region, int target_fps, CGDirectDisplayID display_id) {
    return impl->start(nullptr, target_fps, region, display_id);
}

bool ScreenCapturer::start_region(CGRect region, FrameCallback callback, int target_fps, CGDirectDisplayID display_id) {
    return impl->start(callback, target_fps, region, display_id);
}

void ScreenCapturer::stop() {
    impl->stop();
}

uint64_t ScreenCapturer::get_frame_count() const {
    return impl->delegate ? impl->delegate.frameCount : 0;
}

double ScreenCapturer::get_current_fps() const {
    return impl->delegate ? impl->delegate.currentFPS : 0.0;
}

bool ScreenCapturer::is_running() const {
    return impl->running;
}

bool ScreenCapturer::get_next_frame(uint8_t** out_data, size_t& out_width, 
                                   size_t& out_height, size_t& out_bytes_per_row) {
    if (!impl->running) return false;
    
    std::unique_lock<std::mutex> lock(impl->bufferMutex);
    
    // Wait with check for running status
    impl->bufferCV.wait(lock, [this]{ 
        return impl->delegate.hasNewFrame || !impl->running; 
    });
    
    // Check if we were woken up because stream stopped
    if (!impl->running) return false;
    
    CVPixelBufferRef buffer = impl->delegate.latestBuffer;
    if (!buffer) return false;
    
    // Mark as consumed BEFORE unlocking
    impl->delegate.hasNewFrame = false;
    
    // Unlock previous buffer if locked
    if (impl->lockedBuffer) {
        CVPixelBufferUnlockBaseAddress(impl->lockedBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(impl->lockedBuffer);
        impl->lockedBuffer = nil;
    }
    
    // Retain the new buffer
    impl->lockedBuffer = CVPixelBufferRetain(buffer);
    
    // Unlock mutex before locking pixel buffer
    lock.unlock();
    
    CVPixelBufferLockBaseAddress(impl->lockedBuffer, kCVPixelBufferLock_ReadOnly);
    
    *out_data = (uint8_t*)CVPixelBufferGetBaseAddress(impl->lockedBuffer);
    out_width = CVPixelBufferGetWidth(impl->lockedBuffer);
    out_height = CVPixelBufferGetHeight(impl->lockedBuffer);
    out_bytes_per_row = CVPixelBufferGetBytesPerRow(impl->lockedBuffer);
    
    return true;
}

bool ScreenCapturer::try_get_frame(uint8_t** out_data, size_t& out_width,
                                  size_t& out_height, size_t& out_bytes_per_row) {
    if (!impl->running) return false;
    
    std::unique_lock<std::mutex> lock(impl->bufferMutex);
    
    // Non-blocking: return immediately if no new frame
    if (!impl->delegate.hasNewFrame) {
        return false;
    }
    
    CVPixelBufferRef buffer = impl->delegate.latestBuffer;
    if (!buffer) return false;
    
    // Mark as consumed BEFORE unlocking mutex
    impl->delegate.hasNewFrame = false;
    
    // Unlock previous buffer if locked
    if (impl->lockedBuffer) {
        CVPixelBufferUnlockBaseAddress(impl->lockedBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(impl->lockedBuffer);
        impl->lockedBuffer = nil;
    }
    
    // Retain and lock the new buffer
    impl->lockedBuffer = CVPixelBufferRetain(buffer);
    
    // Unlock mutex BEFORE locking pixel buffer
    lock.unlock();
    
    CVPixelBufferLockBaseAddress(impl->lockedBuffer, kCVPixelBufferLock_ReadOnly);
    
    *out_data = (uint8_t*)CVPixelBufferGetBaseAddress(impl->lockedBuffer);
    out_width = CVPixelBufferGetWidth(impl->lockedBuffer);
    out_height = CVPixelBufferGetHeight(impl->lockedBuffer);
    out_bytes_per_row = CVPixelBufferGetBytesPerRow(impl->lockedBuffer);
    
    return true;
}

bool ScreenCapturer::has_new_frame() const {
    if (!impl->running) return false;
    std::lock_guard<std::mutex> lock(impl->bufferMutex);
    return impl->delegate.hasNewFrame;
}