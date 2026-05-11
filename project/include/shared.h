/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "img_utils.h"
#define BINARY_OUT_FD 3
//#define IPC_DEBUG

#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <string>

class ResponseManager;

extern std::unordered_map<std::string, uint16_t> key_map;
extern std::vector<std::string> shift_required;
extern std::atomic<uint64_t> current_response_id;
extern std::unordered_set<uint64_t> responses;
extern ResponseManager response_manager;
extern double screen_width, screen_height;
extern long window_id;
extern std::mutex ipc_mutex;

bool has_runtime_permission(const std::string& permission);
[[noreturn]] void abort_for_permission_violation(const std::string& permission, const std::string& action);

#pragma pack(push, 1) // Force tight alignment
struct SharedBufferHeader {
    uint64_t frame_index;     // Use volatile or raw, not std::atomic
    uint32_t lock_flag;
    double capture_timestamp;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t data_size;
};

struct RunnerState {
    SharedBufferHeader* shm_header = nullptr; // Initialize to NULL
    uint8_t* pixel_data = nullptr;            // Initialize to NULL
    PixelBuffer buffer;
};
#pragma pack(pop)