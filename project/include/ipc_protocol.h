/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <cstdint>
#include <future>
#include <sys/types.h>
#include "utils.h"

enum class MsgType : uint8_t { 
    //main
    INIT_SCRIPT,    // Sending the initial Lua code
    TRIGGER_EVENT,  // Main App telling Runner a key was pressed
    RESPONSE,

    //runner
    EXIT,          // Terminate the runner
    ADD_KEYBIND,    // Register a new hotkey
    REM_KEYBIND,    // Remove a hotkey
    HEARTBEAT,       // Simple "Are you still alive?" check
    ERROR,

    SYSTEM_MOUSE_CLICK,
    SYSTEM_MOUSE_MOVE,
    SYSTEM_MOUSE_EVENT,
    SYSTEM_MOUSE_REQUEST_POSITION,
    SYSTEM_MOUSE_FULFILL_POSITION,

    SYSTEM_KEYBOARD_PRESS,
    SYSTEM_KEYBOARD_EVENT,
    SYSTEM_KEYBOARD_TYPE,

    SYSTEM_SCREEN_START_CAPTURE,
    SYSTEM_SCREEN_STOP_CAPTURE,
    SYSTEM_SCREEN_METADATA,
    SYSTEM_SCREEN_REQUEST_DIM,
    SYSTEM_SCREEN_FRAME_READY,

    SYSTEM_SCREEN_CANVAS_DRAW_COMMAND,
    SYSTEM_SCREEN_CANVAS_SET_DISPLAY,

    SYSTEM_UI_OPEN,
    SYSTEM_UI_CLOSE,
    SYSTEM_UI_EVENT,
    SYSTEM_UI_RUN_JS,
    SYSTEM_UI_SET_TITLE,
    SYSTEM_UI_SET_SIZE,
    SYSTEM_UI_SET_POSITION,
    SYSTEM_UI_CENTER,
    SYSTEM_UI_SET_VISIBILITY,
    SYSTEM_THROTTLE_SET,
    SYSTEM_THROTTLE_CLEAR,
};

enum class DrawCmdType : uint32_t {
    REMOVE,
    RECT,
    TEXT,
    LINE,
    CLEAR,
    ARC,
    CIRCLE,
    ELLIPSE,
    POLYGON,
    POLYLINE,
    QUADRATIC_CURVE,
    BEZIER_CURVE,
};

inline constexpr uint32_t kDrawCmdFlagCounterclockwise = 1u;
/// Max extra payload (float x,y pairs) for polygon / curve commands over the IPC pipe.
inline constexpr uint32_t kMaxCanvasPolyExtraBytes = 512u * 1024u;


#pragma pack(push, 1) // Ensure no padding between fields for raw pipe writing
struct IPCHeader {
    MsgType type;
    uint32_t payload_size;
    uint64_t request_id;
};
struct MouseData {
    double x;
    double y;
    uint8_t button; // 0 = left, 1 = right, 2 = middle
    uint8_t event_type; // 0 = down, 1 = up
    double duration; // For move events
    int clicks;
};
struct KeyboardData {
    uint8_t event_type; // 0 = down, 1 = up
    uint16_t keycode;
    CGEventFlags flags;
};
struct Response {
    bool success;
};
struct ScreenCaptureRequest {
    size_t x, y, w, h;
    int fps;
    int64_t display_id;
};
struct ScreenMetadata {
    char shm_name[64]; // "/macro_buffer_123"
    uint32_t width;
    uint32_t height;
    uint32_t stride;   // bytes_per_row
};
struct DrawCommand {
    DrawCmdType type;
    float x, y, w, h;
    uint32_t color; // Hex: 0xAAGGBBRR
    float thickness;
    char text[64];  // Fixed size for easy IPC transfer
    char id[32];
    char classes[8][32]; // Up to 8 classes for styling/grouping
    uint32_t class_count=0;
    bool fill=false;
    uint32_t fill_color;
    float angle_start = 0.f;
    float angle_end = 0.f;
    float rotation = 0.f;
    uint32_t flags = 0;
    uint64_t data=0;

    inline bool has_class(const char* name) const {
        for (uint32_t i = 0; i < class_count; ++i) {
            if (std::strncmp(classes[i], name, 32) == 0) return true;
        }
        return false;
    }
};

inline bool draw_cmd_uses_point_payload(DrawCmdType t) {
    switch (t) {
        case DrawCmdType::POLYGON:
        case DrawCmdType::POLYLINE:
        case DrawCmdType::QUADRATIC_CURVE:
        case DrawCmdType::BEZIER_CURVE:
            return true;
        default:
            return false;
    }
}
struct ScreenDim{
    double w;
    double h;
};
struct MousePosition{
    double x;
    double y;
};
struct UIOpenPayload {
    uint32_t window_id;
    uint32_t path_len;
    uint32_t title_len;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t flags;
};
struct UITargetPayload {
    uint32_t window_id;
};
struct UITargetStringPayload {
    uint32_t window_id;
    uint32_t data_len;
};
struct UISetSizePayload {
    uint32_t window_id;
    int32_t width;
    int32_t height;
};
struct UISetPositionPayload {
    uint32_t window_id;
    int32_t x;
    int32_t y;
};
struct UISetVisibilityPayload {
    uint32_t window_id;
    uint8_t visible;
};
struct UIEventPayload {
    uint32_t window_id;
    uint32_t event_len;
    uint32_t payload_len;
};
struct ThrottlePayload {
    uint32_t sleep_us;
};
#pragma pack(pop)

constexpr uint32_t kUiOpenFlagHasX = 1u << 0;
constexpr uint32_t kUiOpenFlagHasY = 1u << 1;
constexpr uint32_t kUiOpenFlagHasWidth = 1u << 2;
constexpr uint32_t kUiOpenFlagHasHeight = 1u << 3;

namespace std {
    template <>
    struct hash<KeyCombo> {
        size_t operator()(const KeyCombo& k) const {
            // Start with a seed
            size_t seed = 0;
            
            // 1. Hash the flags
            seed ^= std::hash<uint64_t>{}(k.flags) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            
            // 2. Hash the count
            seed ^= std::hash<uint8_t>{}(k.count) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            
            // 3. Hash only the active keycodes in the array
            for(int i = 0; i < k.count; ++i) {
                seed ^= std::hash<uint16_t>{}(k.keycodes[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            
            return seed;
        }
    };
}

inline void normalize_combo(KeyCombo& combo) {
    // 1. If count is 0, ensure the whole array is zeroed
    if (combo.count == 0) {
        combo.keycodes.fill(-1);
    } else {
        // 2. Sort only the active keys so [A, B] == [B, A]
        // This ensures the hash is identical regardless of press order
        std::sort(combo.keycodes.begin(), combo.keycodes.begin() + combo.count);
        
        // 3. Zero out the rest of the array to prevent "garbage memory" hashing
        if (combo.count < combo.keycodes.size()) {
            std::fill(combo.keycodes.begin() + combo.count, combo.keycodes.end(), 0);
        }
    }
    // 4. Ensure flags are cleaned (optional: mask out non-essential bits here)
    // combo.flags &= 0xffff0000ULL; 
}

class ResponseManager {
    std::mutex mtx;
    std::unordered_map<uint64_t, std::promise<Response>> pending_requests;
    std::unordered_map<uint64_t, Response> early_responses; // Responses that arrived before wait

public:
    // Call this to wait for a response
    Response wait_for_response(uint64_t id) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Check if response already arrived (race condition case)
        auto early_it = early_responses.find(id);
        if (early_it != early_responses.end()) {
            Response resp = early_it->second;
            early_responses.erase(early_it);
            return resp;
        }
        
        // Response hasn't arrived yet, create promise and wait
        auto fut = pending_requests[id].get_future();
        lock.unlock(); // Release lock before blocking
        
        // Blocks here until fulfill_response is called for this ID
        return fut.get(); 
    }

    // Call this when the response arrives from the other process
    void fulfill_response(uint64_t id, Response data) {
        std::lock_guard<std::mutex> lock(mtx);
        
        auto it = pending_requests.find(id);
        if (it != pending_requests.end()) {
            // Promise exists, fulfill it normally
            it->second.set_value(data);
            pending_requests.erase(it);
        } else {
            // Promise doesn't exist yet - store the response for when wait is called
            // This handles the race where the response arrives before wait_for_response
            early_responses[id] = data;
        }
    }
};