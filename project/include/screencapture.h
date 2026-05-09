//screencapture.h
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <CoreGraphics/CoreGraphics.h>

// Forward declare CoreVideo type (no Objective-C needed in header)
typedef struct __CVBuffer *CVPixelBufferRef;

// Pure C++ callback type
using FrameCallback = std::function<void(CVPixelBufferRef pixelBuffer)>;

class ScreenCapturer {
public:
    ScreenCapturer();
    ~ScreenCapturer();
    
    // Delete copy/move to avoid issues with Objective-C objects
    ScreenCapturer(const ScreenCapturer&) = delete;
    ScreenCapturer& operator=(const ScreenCapturer&) = delete;
    
    // Start capturing full screen
    bool start(int target_fps = 60, CGDirectDisplayID display_id = kCGNullDirectDisplay);
    bool start(FrameCallback callback, int target_fps = 60, CGDirectDisplayID display_id = kCGNullDirectDisplay);
    
    // Start capturing specific region
    bool start_region(CGRect region, int target_fps = 60, CGDirectDisplayID display_id = kCGNullDirectDisplay);
    bool start_region(CGRect region, FrameCallback callback, int target_fps = 60, CGDirectDisplayID display_id = kCGNullDirectDisplay);
    
    // Stop capturing
    void stop();
    
    // Get statistics
    uint64_t get_frame_count() const;
    double get_current_fps() const;
    bool is_running() const;
    
    // Blocking: waits for next frame (original blocking API)
    bool get_next_frame(uint8_t** out_data, size_t& out_width, size_t& out_height, size_t& out_bytes_per_row);
    
    // Non-blocking: returns immediately if no new frame available (RECOMMENDED for game macros)
    // Returns false if no frame, true if frame available
    bool try_get_frame(uint8_t** out_data, size_t& out_width, size_t& out_height, size_t& out_bytes_per_row);
    
    // Check if a new frame is available without retrieving it
    bool has_new_frame() const;
    
private:
    class Impl; // Hide Objective-C implementation
    Impl* impl;
};

void disable_app_nap();
void set_screen_capture_excluded_window_ids(const std::vector<uint32_t>& window_ids);