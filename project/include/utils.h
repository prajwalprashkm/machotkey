/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "shared.h"
#include <array>
#include <iostream>
#include <string>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "sol/sol.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <string_view>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <libproc.h> // For proc_pidinfo if needed

inline std::string get_current_location(sol::this_state &s){
    luaL_where(s, 1);
    std::string location = lua_tostring(s, -1);
    lua_pop(s, 1);
    return location;
}

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>

inline void get_screen_dim(double& width, double& height) {
    auto main_display_id = CGMainDisplayID();
    width = CGDisplayPixelsWide(main_display_id);
    height = CGDisplayPixelsHigh(main_display_id);
}

#endif

bool start_macro_async(const std::string& lua_code, pid_t& out_pid, int& out_write_fd, int& out_read_fd);
void start_macro_async_from_file(const std::string& path, std::atomic<pid_t>& out_pid, std::atomic<int>& out_write, std::atomic<int>& out_read, std::atomic<int>& log_read_pipe);
void start_macro_async_from_string(const std::string& virtual_filename,
                                   const std::string& lua_code, 
                                   std::atomic<pid_t>& out_pid, 
                                   std::atomic<int>& out_write, 
                                   std::atomic<int>& out_read, 
                                   std::atomic<int>& log_read_pipe);
void start_project_macro_async(const std::string& virtual_filename,
                               const std::string& lua_code,
                               const std::string& project_dir,
                               const std::string& sandbox_profile,
                               const std::string& manifest_raw,
                               const std::string& permission_grants_raw,
                               std::atomic<pid_t>& out_pid,
                               std::atomic<int>& out_write,
                               std::atomic<int>& out_read,
                               std::atomic<int>& log_read_pipe);
#pragma pack(push, 1)
struct KeyCombo {
    uint8_t count=0;
    std::array<uint16_t, 16> keycodes;
    CGEventFlags flags = 0;
    bool swallow = false;

    // The map needs this to sort and find entries
    bool operator<(const KeyCombo& other) const {
        if (flags != other.flags) return flags < other.flags;
        if (count != other.count) return count < other.count;
        // std::array already has a lexicographical operator<
        return keycodes < other.keycodes;
    }

    bool operator==(const KeyCombo& other) const {
        return keycodes == other.keycodes && flags == other.flags && count == other.count;
    }
};
#pragma pack(pop)

int32_t get_keycode_for_char(const char* character);
KeyCombo parse_string_to_keybind(std::string input);
KeyCombo parse_string_to_keybind(std::string_view input);
void populate_key_map();

struct ProcessStats {
    double cpu_usage;     // Percentage (0.0 to 100.0 * num_cores)
    size_t ram_bytes;     // Physical footprint
    size_t virtual_ram_bytes;
    uint64_t last_time;   // Internal use for CPU calc
};

ProcessStats get_child_stats(pid_t pid, uint64_t& last_cpu_time, long long interval);

inline double ticks_to_ns_factor() {
    static double factor = 0;
    if (factor == 0) {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        factor = static_cast<double>(info.numer) / info.denom;
    }
    return factor;
}