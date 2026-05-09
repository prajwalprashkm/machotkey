#include "sol/types.hpp"
#define SOL_LUAJIT 1

#include "sol/error.hpp"
#include "sol/optional_implementation.hpp"
#include "sol/protected_function_result.hpp"
#include "sol/sol.hpp"
#include "lua/mouse_bridge.h"
#include "lua/keyboard_bridge.h"
#include "lua/screen_bridge.h"
#include "lua/opencv_bridge.h"
#include "message_queue.h"
#include "ipc_protocol.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <thread>
#include <iostream>
#include <string>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <fstream>
#include <optional>
#include <sstream>
#include <cstring>
#include <climits>
#include "sol/table.hpp"
#include "utils.h"
#include <sandbox.h>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include "shared.h"
#include "shm.h"
#include "color_search.h"
#include "ocr.h"
#include <unistd.h>
#include "img_utils.h"
#include "embed.h"
#include "nlohmann/json.hpp"

extern "C" {
    struct BoundingRectC { size_t x, y, width, height; };
    struct ColorMatchC { bool found; int x, y; };

    ColorMatchC find_exact_color_ffi(const uint8_t* data, size_t w, size_t h, size_t stride, BoundingRectC rect, uint8_t r, uint8_t g, uint8_t b, bool reverse, bool reverse_vertical) {
        ColorRGB target(r, g, b);
        BoundingRect cpp_rect = { rect.x, rect.y, rect.width, rect.height };
        auto res = find_exact_color(data, w, h, stride, cpp_rect, target, reverse, reverse_vertical);
        return { res.found, res.x, res.y };
    }

    ColorMatchC find_fuzzy_color_ffi(const uint8_t* data, size_t w, size_t h, size_t stride, BoundingRectC rect, uint8_t r, uint8_t g, uint8_t b, uint8_t tol, bool reverse, bool reverse_vertical) {
        ColorRGB target(r, g, b);
        BoundingRect cpp_rect = { rect.x, rect.y, rect.width, rect.height };
        auto res = find_color_with_tolerance(data, w, h, stride, cpp_rect, target, tol, reverse, reverse_vertical);
        return { res.found, res.x, res.y };
    }
}

std::atomic<uint64_t> current_response_id = 1;
bool is_polling = false;

bool capturer_running = false;
bool using_callback = false;
std::atomic<bool> frame_ready = false;
sol::protected_function capture_callback;

double screen_width = -1;
double screen_height = -1;

RunnerSHM runner_shm;
FastOCR fast_ocr(true), regular_ocr(false);

std::atomic<bool> global_exit_requested{false};
int global_exit_code = 0;
std::mutex ipc_mutex;
std::atomic<uint32_t> throttle_sleep_us{0};

std::atomic<MousePosition> current_mouse_position;
std::unordered_map<std::string, bool> runtime_permission_grants;
std::unordered_set<std::string> runtime_manifest_lua_files;
std::string runtime_project_dir;
constexpr uint32_t kMaxUiEventNameLen = 128;
constexpr uint32_t kMaxUiPayloadLen = 64 * 1024;
constexpr uint32_t kMaxUiPathLen = 1024;
constexpr uint32_t kMaxUiTitleLen = 256;
constexpr uint32_t kMaxUiWindowCount = 5;
constexpr int32_t kUiDefaultWidth = 900;
constexpr int32_t kUiDefaultHeight = 600;
constexpr int32_t kUiUnsetCoord = INT32_MIN;
std::unordered_set<uint32_t> runtime_ui_window_ids;
uint32_t next_ui_window_id = 1;
uint32_t last_ui_window_id = 0;
std::mutex fs_handles_mutex;
int64_t next_fs_handle_id = 1;

struct RuntimeFileHandle {
    std::fstream stream;
    std::string resolved_path;
    bool readable = false;
    bool writable = false;
};

std::unordered_map<int64_t, RuntimeFileHandle> fs_handles;

struct ExitError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

RunnerState* g_runner_state = nullptr;

enum class CoordMode { Screen = 0, Frame = 1 };
CoordMode g_coord_mode = CoordMode::Screen;

void send_exit(uint32_t exit_code) {
    IPCHeader header = { MsgType::EXIT, exit_code, current_response_id++ };

    ipc_mutex.lock();
    write(BINARY_OUT_FD, &header, sizeof(header));
    ipc_mutex.unlock();
}
void send_response(uint64_t request_id, Response response) {
    IPCHeader header = { MsgType::RESPONSE, sizeof(response), request_id };

    ipc_mutex.lock();
    write(BINARY_OUT_FD, &header, sizeof(header));
    write(BINARY_OUT_FD, &response, sizeof(response));
    ipc_mutex.unlock();
}
void send_error(const std::string& message){
    uint32_t msg_len = static_cast<uint32_t>(message.size());
    IPCHeader header = { MsgType::ERROR, msg_len, current_response_id++ };

    ipc_mutex.lock();
    write(BINARY_OUT_FD, &header, sizeof(header));
    write(BINARY_OUT_FD, message.data(), msg_len);
    ipc_mutex.unlock();
}

Response send_ui_open(uint32_t window_id,
                      const std::string& relative_html_path,
                      const std::string& title,
                      int32_t x, int32_t y,
                      int32_t width, int32_t height,
                      uint32_t flags) {
    if (relative_html_path.empty() || relative_html_path.size() > kMaxUiPathLen) {
        throw std::runtime_error("UI path is empty or too long");
    }
    if (title.size() > kMaxUiTitleLen) {
        throw std::runtime_error("UI title is too long");
    }
    UIOpenPayload payload{
        window_id,
        static_cast<uint32_t>(relative_html_path.size()),
        static_cast<uint32_t>(title.size()),
        x, y, width, height, flags
    };
    const uint64_t request_id = current_response_id++;
    IPCHeader header = {
        MsgType::SYSTEM_UI_OPEN,
        static_cast<uint32_t>(sizeof(payload) + payload.path_len + payload.title_len),
        request_id
    };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
        write(BINARY_OUT_FD, relative_html_path.data(), payload.path_len);
        if (payload.title_len > 0) {
            write(BINARY_OUT_FD, title.data(), payload.title_len);
        }
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_close(uint32_t window_id) {
    UITargetPayload payload{window_id};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_CLOSE, sizeof(payload), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
    }
    return response_manager.wait_for_response(request_id);
}

constexpr uint32_t kMaxUiRunJsLen = 256 * 1024;

Response send_ui_run_js(uint32_t window_id, const std::string& js) {
    if (js.empty() || js.size() > kMaxUiRunJsLen) return {false};
    UITargetStringPayload payload{window_id, static_cast<uint32_t>(js.size())};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_RUN_JS, static_cast<uint32_t>(sizeof(payload) + payload.data_len), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
        write(BINARY_OUT_FD, js.data(), payload.data_len);
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_set_title(uint32_t window_id, const std::string& title) {
    if (title.empty() || title.size() > kMaxUiTitleLen) return {false};
    UITargetStringPayload payload{window_id, static_cast<uint32_t>(title.size())};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_SET_TITLE, static_cast<uint32_t>(sizeof(payload) + payload.data_len), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
        write(BINARY_OUT_FD, title.data(), payload.data_len);
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_set_size(uint32_t window_id, int32_t width, int32_t height) {
    UISetSizePayload payload{window_id, width, height};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_SET_SIZE, sizeof(payload), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_set_position(uint32_t window_id, int32_t x, int32_t y) {
    UISetPositionPayload payload{window_id, x, y};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_SET_POSITION, sizeof(payload), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_center(uint32_t window_id) {
    UITargetPayload payload{window_id};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_CENTER, sizeof(payload), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
    }
    return response_manager.wait_for_response(request_id);
}

Response send_ui_set_visibility(uint32_t window_id, bool visible) {
    UISetVisibilityPayload payload{window_id, static_cast<uint8_t>(visible ? 1 : 0)};
    const uint64_t request_id = current_response_id++;
    IPCHeader header = { MsgType::SYSTEM_UI_SET_VISIBILITY, sizeof(payload), request_id };
    {
        std::lock_guard<std::mutex> lock(ipc_mutex);
        write(BINARY_OUT_FD, &header, sizeof(header));
        write(BINARY_OUT_FD, &payload, sizeof(payload));
    }
    return response_manager.wait_for_response(request_id);
}

bool has_runtime_permission(const std::string& permission) {
    auto it = runtime_permission_grants.find(permission);
    if (it == runtime_permission_grants.end()) return false;
    return it->second;
}

[[noreturn]] void abort_for_permission_violation(const std::string& permission, const std::string& action) {
    const std::string msg = "Permission denied: '" + permission + "' required for " + action;
    send_error(msg);
    global_exit_requested = true;
    global_exit_code = EXIT_FAILURE;
    send_exit(EXIT_FAILURE);
    throw ExitError(msg);
}

std::string file_permission_key(const std::string& path, bool write) {
    return std::string(write ? "file_write:" : "file_read:") + path;
}

bool path_within_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto normalized_root = root.lexically_normal();
    const auto normalized_candidate = candidate.lexically_normal();
    auto root_it = normalized_root.begin();
    auto cand_it = normalized_candidate.begin();
    for (; root_it != normalized_root.end() && cand_it != normalized_candidate.end(); ++root_it, ++cand_it) {
        if (*root_it != *cand_it) return false;
    }
    return root_it == normalized_root.end();
}

std::optional<std::filesystem::path> resolve_runtime_path(const std::string& path_str) {
    if (path_str.empty()) return std::nullopt;
    std::filesystem::path path(path_str);
    if (!path.is_absolute()) {
        path = std::filesystem::path(runtime_project_dir) / path;
    }
    return path.lexically_normal();
}

std::vector<std::string> permission_path_candidates(const std::string& original_path, const std::filesystem::path& resolved) {
    std::vector<std::string> candidates;
    auto add_candidate = [&](const std::string& value) {
        if (value.empty()) return;
        if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
            candidates.push_back(value);
        }
    };

    const std::filesystem::path raw_path(original_path);
    add_candidate(raw_path.lexically_normal().string());
    add_candidate(resolved.lexically_normal().string());

    if (!runtime_project_dir.empty()) {
        const std::filesystem::path root = std::filesystem::path(runtime_project_dir).lexically_normal();
        if (path_within_root(root, resolved)) {
            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(resolved, root, ec);
            if (!ec) {
                add_candidate(relative.lexically_normal().string());
            }
        }
    }
    return candidates;
}

bool has_file_access_permission(const std::string& original_path, const std::filesystem::path& resolved, bool write) {
    for (const auto& candidate : permission_path_candidates(original_path, resolved)) {
        if (has_runtime_permission(file_permission_key(candidate, write))) {
            return true;
        }
    }
    return false;
}

void require_file_permission(const std::string& original_path,
                             const std::filesystem::path& resolved,
                             bool write,
                             const std::string& action) {
    if (has_file_access_permission(original_path, resolved, write)) return;
    const auto candidates = permission_path_candidates(original_path, resolved);
    const std::string key = file_permission_key(candidates.empty() ? resolved.string() : candidates.front(), write);
    abort_for_permission_violation(key, action);
}

struct ScriptStats {
    double cpu_usage;
    uint64_t resident_mem;
    uint64_t virtual_mem;
};

ScriptStats get_own_stats() {
    static uint64_t last_cpu = 0;
    static uint64_t last_time = 0;
    
    struct proc_taskinfo pti;
    if (sizeof(pti) == proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &pti, sizeof(pti))) {
        uint64_t current_cpu = pti.pti_total_user + pti.pti_total_system;
        uint64_t current_time = mach_absolute_time();
        
        double cpu = 0.0;
        if (last_cpu > 0) {
            uint64_t d_cpu = current_cpu - last_cpu;
            uint64_t d_time = current_time - last_time;
            cpu = (static_cast<double>(d_cpu) / d_time) * 100.0;
        }
        
        last_cpu = current_cpu;
        last_time = current_time;

        return { cpu, pti.pti_resident_size, pti.pti_virtual_size };
    }
    return { 0, 0, 0 };
}

void enter_sandbox(const std::string& profile) {
    char *errorbuf = nullptr;

    int result = sandbox_init(profile.c_str(), 0, &errorbuf);

    if (result != 0) {
        fprintf(stderr, "Sandbox failed: %s\n", errorbuf);
        sandbox_free_error(errorbuf);
        exit(EXIT_FAILURE);
    }
}
sol::state lua;
std::map<KeyCombo, sol::protected_function> lua_callbacks;
std::unordered_map<std::string, sol::protected_function> ui_callbacks;
ResponseManager response_manager;
MessageQueue g_inbox;

/*void send_binary_to_main(MsgType type, uint16_t code, uint64_t flags) {
    IPCHeader header = { type, sizeof(KeyCombo) };
    KeyCombo data = { code, flags };
    
    write(BINARY_OUT_FD, &header, sizeof(header));
    write(BINARY_OUT_FD, &data, sizeof(data));
}*/

uint64_t get_id(uint16_t code, uint64_t flags) {
    return (static_cast<uint64_t>(flags) << 16) | (uint64_t)code;
}

inline void apply_cooperative_throttle() {
    const uint32_t sleep_us = throttle_sleep_us.load(std::memory_order_relaxed);
    if (sleep_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }
}

void runner_ipc_listener(int read_fd) {
    while (true) {
        IPCHeader header;
        ssize_t h_bytes = read(read_fd, &header, sizeof(header));
        if (h_bytes <= 0) break;

        if (header.type == MsgType::TRIGGER_EVENT) {
            KeyCombo combo;
            // Read the expected payload size defined in the header
            if (read(read_fd, &combo, sizeof(combo)) > 0) {
                normalize_combo(combo);
                g_inbox.push_event(combo);
            }
        }else if(header.type == MsgType::RESPONSE) {
            if(header.payload_size == sizeof(ScreenDim)) {
                ScreenDim dim;
                if (read(read_fd, &dim, sizeof(dim)) > 0) {
                    screen_width = dim.w;
                    screen_height = dim.h;
                    response_manager.fulfill_response(header.request_id, { true });
                }
            }else{
                Response resp;
                if (read(read_fd, &resp, sizeof(resp)) > 0) {
                    response_manager.fulfill_response(header.request_id, resp);
                }
            }
        }else if (header.type == MsgType::SYSTEM_SCREEN_METADATA) {
            ScreenMetadata meta;
            if (read(read_fd, &meta, sizeof(meta)) > 0) {
                if (runner_shm.connect(meta.shm_name)) {
                    g_runner_state->shm_header = runner_shm.header;
                    g_runner_state->pixel_data = runner_shm.pixel_data;
                    g_runner_state->buffer = {runner_shm.pixel_data, meta.width, meta.height, meta.stride};
                }
            }
        }else if (header.type == MsgType::SYSTEM_SCREEN_FRAME_READY){
            frame_ready = true;
        }else if(header.type == MsgType::SYSTEM_MOUSE_FULFILL_POSITION){
            MousePosition pos;
            if(read(read_fd, &pos, sizeof(pos)) > 0){
                current_mouse_position = pos;
                response_manager.fulfill_response(header.request_id, { true });
            }
        } else if (header.type == MsgType::SYSTEM_UI_EVENT) {
            if (header.payload_size < sizeof(UIEventPayload)) {
                continue;
            }
            UIEventPayload payload{};
            if (read(read_fd, &payload, sizeof(payload)) <= 0) {
                continue;
            }
            if (payload.event_len == 0 ||
                payload.event_len > kMaxUiEventNameLen ||
                payload.payload_len > kMaxUiPayloadLen ||
                header.payload_size != sizeof(payload) + payload.event_len + payload.payload_len) {
                const size_t remaining = payload.event_len + payload.payload_len;
                if (remaining > 0 && remaining <= (4 * 1024 * 1024)) {
                    std::vector<char> discard(remaining);
                    read(read_fd, discard.data(), remaining);
                }
                continue;
            }

            std::string event_name(payload.event_len, '\0');
            if (read(read_fd, event_name.data(), payload.event_len) <= 0) {
                continue;
            }
            std::string event_payload(payload.payload_len, '\0');
            if (payload.payload_len > 0 && read(read_fd, event_payload.data(), payload.payload_len) <= 0) {
                continue;
            }
            g_inbox.push_ui_event(payload.window_id, event_name, event_payload);
        } else if (header.type == MsgType::SYSTEM_THROTTLE_SET) {
            if (header.payload_size != sizeof(ThrottlePayload)) {
                if (header.payload_size > 0 && header.payload_size <= (4 * 1024 * 1024)) {
                    std::vector<char> discard(header.payload_size);
                    read(read_fd, discard.data(), header.payload_size);
                }
                continue;
            }
            ThrottlePayload payload{};
            if (read(read_fd, &payload, sizeof(payload)) <= 0) {
                continue;
            }
            throttle_sleep_us.store(payload.sleep_us, std::memory_order_relaxed);
        } else if (header.type == MsgType::SYSTEM_THROTTLE_CLEAR) {
            if (header.payload_size > 0 && header.payload_size <= (4 * 1024 * 1024)) {
                std::vector<char> discard(header.payload_size);
                read(read_fd, discard.data(), header.payload_size);
            }
            throttle_sleep_us.store(0, std::memory_order_relaxed);
        }
    }
}

void poll_frame_callback() {
    if(is_polling) return;
    is_polling = true;

    if(using_callback && frame_ready && capture_callback.valid()) {
        frame_ready = false;

        lua["system"]["_create_task"](capture_callback);
    }

    is_polling = false;
}

void poll_hotkeys() {
    if(is_polling) return;
    is_polling = true;

    constexpr size_t kMaxHotkeyTasksPerPoll = 256;
    size_t processed = 0;
    KeyCombo combo;
    while (processed < kMaxHotkeyTasksPerPoll && g_inbox.pop_event(combo)) {
        if (lua_callbacks.count(combo)) {
            lua["system"]["_create_task"](lua_callbacks[combo]);
        }
        ++processed;
    }

    static auto last_drop_log = std::chrono::steady_clock::now();
    const auto [dropped_keys, dropped_ui] = g_inbox.take_dropped_counts();
    if ((dropped_keys > 0 || dropped_ui > 0) &&
        std::chrono::steady_clock::now() - last_drop_log > std::chrono::seconds(1)) {
        if (dropped_keys > 0) {
            send_error("Input overload: dropped " + std::to_string(dropped_keys) + " hotkey events to protect stability.");
        }
        if (dropped_ui > 0) {
            send_error("Input overload: dropped " + std::to_string(dropped_ui) + " UI events to protect stability.");
        }
        last_drop_log = std::chrono::steady_clock::now();
    }

    is_polling = false;
}

void poll_ui_events() {
    if (is_polling) return;
    is_polling = true;

    constexpr size_t kMaxUiTasksPerPoll = 128;
    size_t processed = 0;
    uint32_t window_id = 0;
    std::string event_name;
    std::string payload;
    while (processed < kMaxUiTasksPerPoll && g_inbox.pop_ui_event(window_id, event_name, payload)) {
        ++processed;
        auto it = ui_callbacks.find(event_name);
        if (it == ui_callbacks.end()) continue;
        // _create_task does coroutine.create(f) in Lua; f must be a Lua function. wrap_ui_task builds one from callback data.
        sol::protected_function cb = it->second;
        std::string payload_copy = std::move(payload);
        sol::protected_function task = lua["system"]["wrap_ui_task"](cb, payload_copy, window_id);
        lua["system"]["_create_task"](task);
    }

    is_polling = false;
}

void ensure_known_window_id(uint32_t window_id, sol::this_state s) {
    if (runtime_ui_window_ids.count(window_id) == 0) {
        throw sol::error(get_current_location(s) + "Invalid or closed UI window handle");
    }
}

sol::table make_ui_window_handle(sol::this_state s, uint32_t window_id) {
    auto view = sol::state_view(s);
    sol::table handle = view.create_table();
    handle["_id"] = window_id;
    auto run_js_impl = [window_id](const std::string& js, sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_run_js(window_id, js);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to run JS in UI window");
    };
    handle["run_js"] = sol::overload(
        [run_js_impl](const std::string& js, sol::this_state st) { run_js_impl(js, st); },
        [run_js_impl](sol::object, const std::string& js, sol::this_state st) { run_js_impl(js, st); }
    );

    auto close_impl = [window_id](sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_close(window_id);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to close UI window");
        runtime_ui_window_ids.erase(window_id);
        if (last_ui_window_id == window_id) last_ui_window_id = 0;
    };
    handle["close"] = sol::overload(
        [close_impl](sol::this_state st) { close_impl(st); },
        [close_impl](sol::object, sol::this_state st) { close_impl(st); }
    );

    auto set_title_impl = [window_id](const std::string& title, sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_set_title(window_id, title);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to set UI title");
    };
    handle["set_title"] = sol::overload(
        [set_title_impl](const std::string& title, sol::this_state st) { set_title_impl(title, st); },
        [set_title_impl](sol::object, const std::string& title, sol::this_state st) { set_title_impl(title, st); }
    );

    auto set_size_impl = [window_id](int width, int height, sol::this_state st) {
        ensure_known_window_id(window_id, st);
        if (width <= 0 || height <= 0) throw sol::error(get_current_location(st) + "width/height must be > 0");
        Response resp = send_ui_set_size(window_id, width, height);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to set UI size");
    };
    handle["set_size"] = sol::overload(
        [set_size_impl](int width, int height, sol::this_state st) { set_size_impl(width, height, st); },
        [set_size_impl](sol::object, int width, int height, sol::this_state st) { set_size_impl(width, height, st); }
    );

    auto set_pos_impl = [window_id](int x, int y, sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_set_position(window_id, x, y);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to set UI position");
    };
    handle["set_position"] = sol::overload(
        [set_pos_impl](int x, int y, sol::this_state st) { set_pos_impl(x, y, st); },
        [set_pos_impl](sol::object, int x, int y, sol::this_state st) { set_pos_impl(x, y, st); }
    );

    auto center_impl = [window_id](sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_center(window_id);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to center UI window");
    };
    handle["center"] = sol::overload(
        [center_impl](sol::this_state st) { center_impl(st); },
        [center_impl](sol::object, sol::this_state st) { center_impl(st); }
    );

    auto show_impl = [window_id](bool visible, sol::this_state st) {
        ensure_known_window_id(window_id, st);
        Response resp = send_ui_set_visibility(window_id, visible);
        if (!resp.success) throw sol::error(get_current_location(st) + "Failed to change UI visibility");
    };
    handle["show"] = sol::overload(
        [show_impl](sol::this_state st) { show_impl(true, st); },
        [show_impl](sol::object, sol::this_state st) { show_impl(true, st); }
    );
    handle["hide"] = sol::overload(
        [show_impl](sol::this_state st) { show_impl(false, st); },
        [show_impl](sol::object, sol::this_state st) { show_impl(false, st); }
    );
    return handle;
}

sol::table l_ui_open(const std::string& relative_html_path, sol::optional<sol::table> options, sol::this_state s) {
    if (runtime_ui_window_ids.size() >= kMaxUiWindowCount) {
        throw sol::error(get_current_location(s) + "UI window limit reached (" + std::to_string(kMaxUiWindowCount) + ")");
    }
    if (relative_html_path.empty() || relative_html_path.size() > kMaxUiPathLen) {
        throw sol::error(get_current_location(s) + "Invalid UI path");
    }

    std::string title = "Macro UI";
    int32_t x = kUiUnsetCoord;
    int32_t y = kUiUnsetCoord;
    int32_t width = kUiDefaultWidth;
    int32_t height = kUiDefaultHeight;
    uint32_t flags = 0;

    if (options) {
        if ((*options)["title"].valid() && (*options)["title"].get_type() == sol::type::string) {
            title = (*options)["title"].get<std::string>();
        }
        if ((*options)["x"].valid() && (*options)["x"].get_type() == sol::type::number) {
            x = (*options)["x"].get<int32_t>();
            flags |= kUiOpenFlagHasX;
        }
        if ((*options)["y"].valid() && (*options)["y"].get_type() == sol::type::number) {
            y = (*options)["y"].get<int32_t>();
            flags |= kUiOpenFlagHasY;
        }
        if ((*options)["width"].valid() && (*options)["width"].get_type() == sol::type::number) {
            width = (*options)["width"].get<int32_t>();
            flags |= kUiOpenFlagHasWidth;
        } else if ((*options)["w"].valid() && (*options)["w"].get_type() == sol::type::number) {
            width = (*options)["w"].get<int32_t>();
            flags |= kUiOpenFlagHasWidth;
        }
        if ((*options)["height"].valid() && (*options)["height"].get_type() == sol::type::number) {
            height = (*options)["height"].get<int32_t>();
            flags |= kUiOpenFlagHasHeight;
        } else if ((*options)["h"].valid() && (*options)["h"].get_type() == sol::type::number) {
            height = (*options)["h"].get<int32_t>();
            flags |= kUiOpenFlagHasHeight;
        }
    }

    if (width <= 0 || height <= 0) {
        throw sol::error(get_current_location(s) + "width/height must be > 0");
    }

    uint32_t window_id = next_ui_window_id++;
    if (window_id == 0) window_id = next_ui_window_id++;

    Response resp = send_ui_open(window_id, relative_html_path, title, x, y, width, height, flags);
    if (!resp.success) {
        throw sol::error(get_current_location(s) + "Failed to open UI window");
    }

    runtime_ui_window_ids.insert(window_id);
    last_ui_window_id = window_id;
    return make_ui_window_handle(s, window_id);
}

void l_ui_close(sol::optional<uint32_t> window_id, sol::this_state s) {
    uint32_t target = window_id.value_or(last_ui_window_id);
    if (target == 0) return;
    ensure_known_window_id(target, s);
    Response resp = send_ui_close(target);
    if (!resp.success) throw sol::error(get_current_location(s) + "Failed to close UI window");
    runtime_ui_window_ids.erase(target);
    if (last_ui_window_id == target) last_ui_window_id = 0;
}

void l_ui_run_js(const std::string& js, sol::optional<uint32_t> window_id, sol::this_state s) {
    uint32_t target = window_id.value_or(last_ui_window_id);
    if (target == 0) throw sol::error(get_current_location(s) + "No active UI window");
    ensure_known_window_id(target, s);
    Response resp = send_ui_run_js(target, js);
    if (!resp.success) throw sol::error(get_current_location(s) + "Failed to run JS in UI window");
}

void l_ui_on(const std::string& event_name, sol::protected_function callback, sol::this_state s) {
    if (event_name.empty() || event_name.size() > kMaxUiEventNameLen) {
        throw sol::error(get_current_location(s) + "Invalid UI event name");
    }
    ui_callbacks[event_name] = callback;
}

void l_ui_off(const std::string& event_name) {
    ui_callbacks.erase(event_name);
}

void l_on_key_opt(std::string key_pattern, sol::optional<sol::table> options, sol::protected_function callback, sol::this_state s) {
    if (!has_runtime_permission("keyboard_listen")) {
        abort_for_permission_violation("keyboard_listen", "system.on_key");
    }
    bool swallow = options ? options->get_or("swallow", false) : false;

    // 1. Use your parser
    KeyCombo combo = parse_string_to_keybind(key_pattern);

    combo.swallow = swallow;
    
    normalize_combo(combo);
    if (combo.keycodes[0] == 0xFFFF) {
        throw sol::error(get_current_location(s)+"Invalid keybind string: " + key_pattern);
        return;
    }

    // 2. Store the callback in the Runner's registry
    // Combine flags and keycode into a unique 64-bit ID for the map
    lua_callbacks[combo] = callback;

    // 3. Send binary "ADD_KEYBIND" to the Main App via fd 3
    IPCHeader header = { MsgType::ADD_KEYBIND, sizeof(KeyCombo), current_response_id++};

    // Send the header then the payload
    ipc_mutex.lock();
    write(BINARY_OUT_FD, &header, sizeof(header));
    write(BINARY_OUT_FD, &combo, sizeof(combo));
    ipc_mutex.unlock();
}

void l_on_key_no_opt(std::string key_pattern, sol::protected_function callback, sol::this_state s) {
    l_on_key_opt(key_pattern, sol::nullopt, callback, s);
}

void l_remove_keybind(std::string key_pattern, sol::this_state s) {
    // 1. Use your parser
    KeyCombo combo = parse_string_to_keybind(key_pattern);
    
    normalize_combo(combo);
    if (combo.keycodes[0] == 0xFFFF) {
        throw sol::error(get_current_location(s)+"Invalid keybind string: " + key_pattern);
        return;
    }

    // 2. Remove from the Runner's registry
    lua_callbacks.erase(combo);

    // 3. Send binary "REM_KEYBIND" to the Main App via fd 3
    IPCHeader header = { MsgType::REM_KEYBIND, sizeof(KeyCombo), current_response_id++};

    // Send the header then the payload
    ipc_mutex.lock();
    write(BINARY_OUT_FD, &header, sizeof(header));
    write(BINARY_OUT_FD, &combo, sizeof(combo));
    ipc_mutex.unlock();
}

int l_set_coord_mode(std::string mode, sol::this_state s) {
    if (mode == "screen") {
        g_coord_mode = CoordMode::Screen;
    } else if (mode == "frame") {
        g_coord_mode = CoordMode::Frame;
    } else {
        throw sol::error(get_current_location(s) + "Invalid coordinate mode: " + mode + ". Expected 'screen' or 'frame'.");
    }
    return 0;
}

int l_get_coord_mode() {
    return static_cast<int>(g_coord_mode);
}

std::ios::openmode parse_open_mode(const std::string& mode,
                                   bool& readable,
                                   bool& writable) {
    readable = false;
    writable = false;

    if (mode == "r" || mode == "rb") {
        readable = true;
        return std::ios::in | std::ios::binary;
    }
    if (mode == "w" || mode == "wb") {
        writable = true;
        return std::ios::out | std::ios::trunc | std::ios::binary;
    }
    if (mode == "a" || mode == "ab") {
        writable = true;
        return std::ios::out | std::ios::app | std::ios::binary;
    }
    if (mode == "r+" || mode == "rb+" || mode == "r+b") {
        readable = true;
        writable = true;
        return std::ios::in | std::ios::out | std::ios::binary;
    }
    if (mode == "w+" || mode == "wb+" || mode == "w+b") {
        readable = true;
        writable = true;
        return std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary;
    }
    if (mode == "a+" || mode == "ab+" || mode == "a+b") {
        readable = true;
        writable = true;
        return std::ios::in | std::ios::out | std::ios::app | std::ios::binary;
    }
    throw std::runtime_error("Invalid file mode: " + mode);
}

void require_any_file_permission(const std::string& original_path,
                                 const std::filesystem::path& resolved,
                                 const std::string& action) {
    if (has_file_access_permission(original_path, resolved, false) ||
        has_file_access_permission(original_path, resolved, true)) {
        return;
    }
    const auto candidates = permission_path_candidates(original_path, resolved);
    const std::string key = file_permission_key(candidates.empty() ? resolved.string() : candidates.front(), false);
    abort_for_permission_violation(key, action);
}

RuntimeFileHandle& get_file_handle_or_throw(int64_t handle_id, sol::this_state s) {
    std::lock_guard<std::mutex> lock(fs_handles_mutex);
    auto it = fs_handles.find(handle_id);
    if (it == fs_handles.end()) {
        throw sol::error(get_current_location(s) + "Invalid file handle");
    }
    return it->second;
}

int64_t l_fs_open(const std::string& path, sol::optional<std::string> mode_opt) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;

    bool readable = false;
    bool writable = false;
    const std::string mode = mode_opt.value_or("r");
    const auto open_mode = parse_open_mode(mode, readable, writable);

    if (readable) require_file_permission(path, resolved, false, "system.fs.open");
    if (writable) require_file_permission(path, resolved, true, "system.fs.open");

    if (writable) {
        std::error_code ec;
        std::filesystem::create_directories(resolved.parent_path(), ec);
    }

    RuntimeFileHandle handle;
    handle.readable = readable;
    handle.writable = writable;
    handle.resolved_path = resolved.string();
    handle.stream.open(resolved, open_mode);
    if (!handle.stream.is_open()) {
        throw std::runtime_error("Failed to open file: " + resolved.string());
    }

    std::lock_guard<std::mutex> lock(fs_handles_mutex);
    const int64_t id = next_fs_handle_id++;
    fs_handles.emplace(id, std::move(handle));
    return id;
}

sol::object l_fs_read(sol::this_state s, int64_t handle_id, sol::object spec) {
    RuntimeFileHandle& handle = get_file_handle_or_throw(handle_id, s);
    if (!handle.readable) throw sol::error(get_current_location(s) + "File not opened for reading");
    if (!handle.stream.is_open()) throw sol::error(get_current_location(s) + "File handle is closed");

    if (!spec.valid() || spec.get_type() == sol::type::lua_nil) {
        std::ostringstream oss;
        oss << handle.stream.rdbuf();
        return sol::make_object(s, oss.str());
    }

    if (spec.get_type() == sol::type::number) {
        const int64_t count = spec.as<int64_t>();
        if (count <= 0) return sol::make_object(s, std::string());
        std::string out(static_cast<size_t>(count), '\0');
        handle.stream.read(out.data(), count);
        const std::streamsize bytes = handle.stream.gcount();
        if (bytes <= 0) return sol::make_object(s, sol::lua_nil);
        out.resize(static_cast<size_t>(bytes));
        return sol::make_object(s, out);
    }

    const std::string mode = spec.as<std::string>();
    if (mode == "*l") {
        std::string line;
        if (!std::getline(handle.stream, line)) {
            return sol::make_object(s, sol::lua_nil);
        }
        return sol::make_object(s, line);
    }
    if (mode == "*a") {
        std::ostringstream oss;
        oss << handle.stream.rdbuf();
        return sol::make_object(s, oss.str());
    }
    throw sol::error(get_current_location(s) + "Unsupported read mode: " + mode);
}

std::string l_fs_read_line(sol::this_state s, int64_t handle_id) {
    RuntimeFileHandle& handle = get_file_handle_or_throw(handle_id, s);
    if (!handle.readable) throw sol::error(get_current_location(s) + "File not opened for reading");
    if (!handle.stream.is_open()) throw sol::error(get_current_location(s) + "File handle is closed");
    std::string line;
    if (!std::getline(handle.stream, line)) return "";
    return line;
}

bool l_fs_write(sol::this_state s, int64_t handle_id, const std::string& data) {
    RuntimeFileHandle& handle = get_file_handle_or_throw(handle_id, s);
    if (!handle.writable) throw sol::error(get_current_location(s) + "File not opened for writing");
    if (!handle.stream.is_open()) throw sol::error(get_current_location(s) + "File handle is closed");
    handle.stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return !handle.stream.fail();
}

double l_fs_seek(sol::this_state s, int64_t handle_id, sol::optional<std::string> whence_opt, sol::optional<int64_t> offset_opt) {
    RuntimeFileHandle& handle = get_file_handle_or_throw(handle_id, s);
    if (!handle.stream.is_open()) throw sol::error(get_current_location(s) + "File handle is closed");

    std::ios::seekdir dir = std::ios::beg;
    const std::string whence = whence_opt.value_or("cur");
    if (whence == "set") dir = std::ios::beg;
    else if (whence == "cur") dir = std::ios::cur;
    else if (whence == "end") dir = std::ios::end;
    else throw sol::error(get_current_location(s) + "seek whence must be set/cur/end");

    const int64_t offset = offset_opt.value_or(0);
    handle.stream.clear();
    handle.stream.seekg(offset, dir);
    handle.stream.seekp(offset, dir);
    if (handle.stream.fail()) {
        throw sol::error(get_current_location(s) + "seek failed");
    }
    std::streampos pos = handle.stream.tellg();
    if (pos < 0) {
        pos = handle.stream.tellp();
    }
    return static_cast<double>(pos);
}

bool l_fs_flush(sol::this_state s, int64_t handle_id) {
    RuntimeFileHandle& handle = get_file_handle_or_throw(handle_id, s);
    if (!handle.stream.is_open()) throw sol::error(get_current_location(s) + "File handle is closed");
    handle.stream.flush();
    return !handle.stream.fail();
}

void l_fs_close(sol::this_state s, int64_t handle_id) {
    std::lock_guard<std::mutex> lock(fs_handles_mutex);
    auto it = fs_handles.find(handle_id);
    if (it == fs_handles.end()) {
        throw sol::error(get_current_location(s) + "Invalid file handle");
    }
    if (it->second.stream.is_open()) {
        it->second.stream.close();
    }
    fs_handles.erase(it);
}

std::string l_fs_read_all(const std::string& path) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_file_permission(path, resolved, false, "system.fs.read_all");

    std::ifstream in(resolved, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Failed to open file: " + resolved.string());
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

bool l_fs_write_all(const std::string& path, const std::string& data, sol::optional<bool> append_opt) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_file_permission(path, resolved, true, "system.fs.write_all");

    std::error_code ec;
    std::filesystem::create_directories(resolved.parent_path(), ec);
    const bool append = append_opt.value_or(false);
    std::ofstream out(resolved, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out.is_open()) throw std::runtime_error("Failed to open file: " + resolved.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return !out.fail();
}

bool l_fs_exists(const std::string& path) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_any_file_permission(path, resolved, "system.fs.exists");
    std::error_code ec;
    return std::filesystem::exists(resolved, ec);
}

sol::table l_fs_stat(sol::this_state s, const std::string& path) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_any_file_permission(path, resolved, "system.fs.stat");

    std::error_code ec;
    sol::table out = sol::state_view(s).create_table();
    out["path"] = resolved.string();
    const bool exists = std::filesystem::exists(resolved, ec);
    out["exists"] = exists;
    if (ec || !exists) {
        out["is_file"] = false;
        out["is_dir"] = false;
        out["size"] = 0.0;
        return out;
    }

    const bool is_file = std::filesystem::is_regular_file(resolved, ec);
    out["is_file"] = is_file;
    out["is_dir"] = std::filesystem::is_directory(resolved, ec);
    if (!ec && is_file) {
        out["size"] = static_cast<double>(std::filesystem::file_size(resolved, ec));
    } else {
        out["size"] = 0.0;
    }
    return out;
}

sol::table l_fs_list(sol::this_state s, sol::optional<std::string> dir_opt) {
    const std::string dir = dir_opt.value_or(".");
    const auto resolved_opt = resolve_runtime_path(dir);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved_dir = resolved_opt->lexically_normal();

    sol::table out = sol::state_view(s).create_table();
    size_t idx = 1;
    for (const auto& [permission, allowed] : runtime_permission_grants) {
        if (!allowed) continue;
        constexpr const char* read_prefix = "file_read:";
        constexpr const char* write_prefix = "file_write:";
        std::string path_value;
        if (permission.rfind(read_prefix, 0) == 0) {
            path_value = permission.substr(std::strlen(read_prefix));
        } else if (permission.rfind(write_prefix, 0) == 0) {
            path_value = permission.substr(std::strlen(write_prefix));
        } else {
            continue;
        }

        const auto entry_resolved_opt = resolve_runtime_path(path_value);
        if (!entry_resolved_opt) continue;
        const auto entry_resolved = entry_resolved_opt->lexically_normal();
        if (!path_within_root(resolved_dir, entry_resolved)) continue;
        out[idx++] = entry_resolved.string();
    }
    return out;
}

bool l_fs_remove(const std::string& path) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_file_permission(path, resolved, true, "system.fs.remove");

    std::error_code ec;
    if (std::filesystem::is_directory(resolved, ec)) {
        std::filesystem::remove_all(resolved, ec);
        return !ec;
    }
    return std::filesystem::remove(resolved, ec);
}

bool l_fs_mkdir(const std::string& path, sol::optional<bool> recursive_opt) {
    const auto resolved_opt = resolve_runtime_path(path);
    if (!resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto resolved = *resolved_opt;
    require_file_permission(path, resolved, true, "system.fs.mkdir");

    std::error_code ec;
    const bool recursive = recursive_opt.value_or(true);
    if (recursive) {
        std::filesystem::create_directories(resolved, ec);
    } else {
        std::filesystem::create_directory(resolved, ec);
    }
    return !ec;
}

bool l_fs_rename(const std::string& src, const std::string& dst) {
    const auto src_resolved_opt = resolve_runtime_path(src);
    const auto dst_resolved_opt = resolve_runtime_path(dst);
    if (!src_resolved_opt || !dst_resolved_opt) throw std::runtime_error("Path cannot be empty");
    const auto src_resolved = *src_resolved_opt;
    const auto dst_resolved = *dst_resolved_opt;
    require_file_permission(src, src_resolved, true, "system.fs.rename");
    require_file_permission(dst, dst_resolved, true, "system.fs.rename");

    std::error_code ec;
    std::filesystem::create_directories(dst_resolved.parent_path(), ec);
    ec.clear();
    std::filesystem::rename(src_resolved, dst_resolved, ec);
    return !ec;
}

int main(int argc, char* argv[]) {
    std::thread ppid_poller([](){
        while(getppid() != 1){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::exit(0);
        return;
    });

    ppid_poller.detach();
    populate_key_map();
    setvbuf(stdout, NULL, _IONBF, 0); // Disable stdout buffering
    setvbuf(stderr, NULL, _IONBF, 0); // Disable stderr buffering

    // --- PHASE 1: Read Script (Text Mode) ---
    // We read from stdin until the Main App sends the delimiter

    uint32_t filename_len;
    if(!std::cin.read(reinterpret_cast<char*>(&filename_len), sizeof(filename_len))){
        std::cerr << "Runner: Failed to read filename length." << std::endl;
        return 1;
    }
    if(filename_len == 0 || filename_len > 4096){
        std::cerr << "Runner: Invalid filename length." << std::endl;
        return 1;
    }
    std::vector<char> filename_buff(filename_len);
    if(!std::cin.read(filename_buff.data(), filename_len)){
        std::cerr << "Runner: Failed to read filename." << std::endl;
        return 1;
    }
    std::string filename(filename_buff.begin(), filename_buff.end());

    uint32_t code_len;
    if(!std::cin.read(reinterpret_cast<char*>(&code_len), sizeof(code_len))){
        std::cerr << "Runner: Failed to read code length." << std::endl;
        return 1;
    }
    if(code_len == 0 || code_len > 50 * 1024 * 1024){
        std::cerr << "Runner: Invalid code length." << std::endl;
        return 1;
    }
    std::vector<char> code_buff(code_len);
    if(!std::cin.read(code_buff.data(), code_len)){
        std::cerr << "Runner: Failed to read code." << std::endl;
        return 1;
    }
    std::string lua_code(code_buff.begin(), code_buff.end());
    uint32_t project_dir_len;
    if(!std::cin.read(reinterpret_cast<char*>(&project_dir_len), sizeof(project_dir_len))){
        std::cerr << "Runner: Failed to read project directory length." << std::endl;
        return 1;
    }
    if(project_dir_len == 0 || project_dir_len > 16 * 1024){
        std::cerr << "Runner: Invalid project directory length." << std::endl;
        return 1;
    }
    std::vector<char> project_dir_buff(project_dir_len);
    if(!std::cin.read(project_dir_buff.data(), project_dir_len)){
        std::cerr << "Runner: Failed to read project directory." << std::endl;
        return 1;
    }
    std::string project_dir(project_dir_buff.begin(), project_dir_buff.end());

    uint32_t sandbox_len;
    if(!std::cin.read(reinterpret_cast<char*>(&sandbox_len), sizeof(sandbox_len))){
        std::cerr << "Runner: Failed to read sandbox profile length." << std::endl;
        return 1;
    }
    if(sandbox_len == 0 || sandbox_len > 4 * 1024 * 1024){
        std::cerr << "Runner: Invalid sandbox profile length." << std::endl;
        return 1;
    }
    std::vector<char> sandbox_buff(sandbox_len);
    if(!std::cin.read(sandbox_buff.data(), sandbox_len)){
        std::cerr << "Runner: Failed to read sandbox profile." << std::endl;
        return 1;
    }
    std::string sandbox_profile(sandbox_buff.begin(), sandbox_buff.end());

    uint32_t manifest_len;
    if(!std::cin.read(reinterpret_cast<char*>(&manifest_len), sizeof(manifest_len))){
        std::cerr << "Runner: Failed to read manifest length." << std::endl;
        return 1;
    }
    if(manifest_len == 0 || manifest_len > 4 * 1024 * 1024){
        std::cerr << "Runner: Invalid manifest length." << std::endl;
        return 1;
    }
    std::vector<char> manifest_buff(manifest_len);
    if(!std::cin.read(manifest_buff.data(), manifest_len)){
        std::cerr << "Runner: Failed to read manifest." << std::endl;
        return 1;
    }
    std::string manifest_raw(manifest_buff.begin(), manifest_buff.end());

    uint32_t grants_len;
    if(!std::cin.read(reinterpret_cast<char*>(&grants_len), sizeof(grants_len))){
        std::cerr << "Runner: Failed to read permission grants length." << std::endl;
        return 1;
    }
    if(grants_len == 0 || grants_len > 1024 * 1024){
        std::cerr << "Runner: Invalid permission grants length." << std::endl;
        return 1;
    }
    std::vector<char> grants_buff(grants_len);
    if(!std::cin.read(grants_buff.data(), grants_len)){
        std::cerr << "Runner: Failed to read permission grants." << std::endl;
        return 1;
    }
    std::string permission_grants_raw(grants_buff.begin(), grants_buff.end());

    enter_sandbox(sandbox_profile);

    try {
        runtime_project_dir = std::filesystem::path(project_dir).lexically_normal().string();
        auto manifest_json = nlohmann::json::parse(manifest_raw);

        auto add_manifest_file = [&](const std::string& file_value) {
            if (file_value.empty() || file_value.back() == '/') return;
            std::filesystem::path file_path(file_value);
            std::filesystem::path resolved = file_path.is_absolute()
                ? file_path
                : (std::filesystem::path(runtime_project_dir) / file_path);
            resolved = resolved.lexically_normal();
            if (resolved.extension() == ".lua") {
                runtime_manifest_lua_files.insert(resolved.string());
            }
        };

        if (manifest_json.contains("entry") && manifest_json["entry"].is_string()) {
            add_manifest_file(manifest_json["entry"].get<std::string>());
        }

        if (manifest_json.contains("files") && manifest_json["files"].is_array()) {
            for (const auto& file : manifest_json["files"]) {
                if (file.is_string()) {
                    add_manifest_file(file.get<std::string>());
                } else if (file.is_object() && file.contains("path") && file["path"].is_string()) {
                    add_manifest_file(file["path"].get<std::string>());
                }
            }
        }

        if (manifest_json.contains("permissions") && manifest_json["permissions"].is_array()) {
            for (const auto& permission : manifest_json["permissions"]) {
                if (permission.is_object() && permission.contains("name") && permission["name"].is_string()) {
                    runtime_permission_grants[permission["name"].get<std::string>()] = false;
                }
            }
        }

        auto grants_json = nlohmann::json::parse(permission_grants_raw);
        if (grants_json.is_object()) {
            for (auto it = grants_json.begin(); it != grants_json.end(); ++it) {
                runtime_permission_grants[it.key()] = it.value().get<bool>();
            }
        }
    } catch (const std::exception& e) {
        send_error(std::string("Failed to parse permission grants: ") + e.what());
        send_exit(EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    current_mouse_position.store({0, 0});
    
    // 1. Setup Lua Environment
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table, sol::lib::ffi, sol::lib::math, sol::lib::debug, sol::lib::bit32, sol::lib::jit);

    sol::table system = lua.create_table();

    // The bridge between the IPC queue and Lua execution
    system["_poll_events"] = []() { 
        poll_frame_callback();
        poll_hotkeys();
        poll_ui_events();
    };
    system["_project_dir"] = runtime_project_dir;
    {
        sol::table allowed = lua.create_table();
        for (const auto& file : runtime_manifest_lua_files) {
            allowed[file] = true;
        }
        system["_manifest_lua_files"] = allowed;
    }

    // Update your system["wait"] definition in macro_runner.cpp
    system["_cpp_wait"] = [](double duration, sol::object unit_obj) {
        long long total_us = 0;
        std::string unit = unit_obj.is<std::string>() ? unit_obj.as<std::string>() : "s";

        if (unit == "s")       total_us = static_cast<long long>(duration * 1000000.0);
        else if (unit == "ms") total_us = static_cast<long long>(duration * 1000.0);
        else if (unit == "us") total_us = static_cast<long long>(duration);

        auto start = std::chrono::steady_clock::now();
        
        while (true) {
            poll_frame_callback();
            poll_hotkeys();
            poll_ui_events();
            lua["system"]["_update_tasks"]();
            apply_cooperative_throttle();

            auto now = std::chrono::steady_clock::now();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            long long remaining_us = total_us - elapsed_us;

            if (remaining_us <= 0) break;

            // Adaptive Sleeping
            if (remaining_us > 5000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (remaining_us > 1000) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                // Under 1ms, we just yield to keep the IPC thread alive 
                // without the overhead of a full sleep context-switch
                std::this_thread::yield();
            }
        }
    };

    system["get_time"] = [](sol::optional<std::string> unit_obj) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        std::string unit = unit_obj.value_or("s");

        if (unit == "ms") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        } else if (unit == "us") {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
        }
        
        // Default to seconds (double for fractional parts)
        return std::chrono::duration_cast<std::chrono::duration<double>>(now).count();
    };

    system["exit"] = [](std::optional<int> exit_code) {
        global_exit_requested = true;
        global_exit_code = exit_code.value_or(0);
        send_exit(global_exit_code);

        // We still throw to try and break the current Lua execution path
        //throw exit_error("EXIT"); 
    };

    // 1. Create the 'stats' table in Lua
    sol::table stats = lua.create_table();

    // 2. Bind the function to that table
    stats["get_info"] = [](sol::optional<std::string> unit) {
        // Note: Use sol::optional instead of std::optional for better compatibility
        std::string val = unit.value_or("gb");
        
        double divisor;
        if (val == "b") {
            divisor = 1.0;
        } else if (val == "kb") {
            divisor = 1024.0;
        } else if (val == "mb") {
            divisor = 1024.0 * 1024.0;
        } else {
            divisor = 1024.0 * 1024.0 * 1024.0;
        }

        auto stats_data = get_own_stats();
        
        return lua.create_table_with(
            "cpu", stats_data.cpu_usage,
            "ram", static_cast<double>(stats_data.resident_mem) / divisor,
            "vmem", static_cast<double>(stats_data.virtual_mem) / divisor
        );
    };

    system.set_function("_yield", []() {
        std::this_thread::yield(); 
    });

    system["stats"] = stats;

    sol::table permissions = lua.create_table();
    permissions["get"] = [](sol::optional<std::string> permission) -> sol::object {
        if (permission.has_value()) {
            bool allowed = has_runtime_permission(permission.value());
            return sol::make_object(lua, allowed);
        }

        sol::table ret = lua.create_table();
        for (const auto& [name, allowed] : runtime_permission_grants) {
            ret[name] = allowed;
        }
        return sol::make_object(lua, ret);
    };
    system["permissions"] = permissions;

    system.set_function("on_key", sol::overload(&l_on_key_no_opt, &l_on_key_opt));
    system["remove_keybind"] = &l_remove_keybind;
    sol::table ui = lua.create_table();
    ui["open"] = sol::overload(
        [](const std::string& relative_html_path, sol::this_state s) {
            return l_ui_open(relative_html_path, sol::nullopt, s);
        },
        [](const std::string& relative_html_path, sol::table options, sol::this_state s) {
            return l_ui_open(relative_html_path, sol::optional<sol::table>(options), s);
        }
    );
    ui["close"] = &l_ui_close;
    ui["on"] = &l_ui_on;
    ui["off"] = &l_ui_off;
    ui["run_js"] = &l_ui_run_js;
    system["ui"] = ui;
    system["register_ui_callback"] = &l_ui_on;
    system["unregister_ui_callback"] = &l_ui_off;

    sol::table fs = lua.create_table();
    fs["open"] = &l_fs_open;
    fs["read_all"] = &l_fs_read_all;
    fs["write_all"] = &l_fs_write_all;
    fs["append"] = [](const std::string& path, const std::string& data) {
        return l_fs_write_all(path, data, true);
    };
    fs["exists"] = &l_fs_exists;
    fs["stat"] = &l_fs_stat;
    fs["list"] = &l_fs_list;
    fs["remove"] = &l_fs_remove;
    fs["mkdir"] = &l_fs_mkdir;
    fs["rename"] = &l_fs_rename;
    fs["_read"] = &l_fs_read;
    fs["_read_line"] = &l_fs_read_line;
    fs["_write"] = &l_fs_write;
    fs["_seek"] = &l_fs_seek;
    fs["_flush"] = &l_fs_flush;
    fs["_close"] = &l_fs_close;
    system["fs"] = fs;

    lua["system"] = system;

    // Global or class member that lives as long as the Runner
    g_runner_state = new RunnerState(); 

    // Register external bridges (Mouse, etc.)
    MouseBridge::register_code(lua, system);
    KeyboardBridge::register_code(lua, system);
    ScreenBridge::register_code(lua, system);

    //lua["system"]["screen"]["set_coord_mode"] = &l_set_coord_mode;
    //lua["system"]["screen"]["get_coord_mode"] = &l_get_coord_mode;

    // Pass this PERMANENT address to Lua once
    lua["system"]["_state_addr"] = reinterpret_cast<uintptr_t>(g_runner_state);

    // Build a Lua-callable thunk for system._create_task (which uses coroutine.create).
    lua["system"]["wrap_ui_task"] = [](sol::protected_function cb, const std::string& p, uint32_t window_id) {
        return sol::as_function([cb, p, window_id]() {
            const auto r = cb(p, window_id);
            if (!r.valid()) {
                const sol::error err = r;
                send_error(std::string("UI callback error: ") + err.what());
            }
        });
    };

    // After setting up 'system' table:
    lua.script(BOOTSTRAP_LUA);

    // --- PHASE 2: Start Binary IPC (Binary Mode) ---
    // Now that the text stream is finished, we spawn the binary listener
    // It will continue reading from the same stdin (0)
    std::thread listener(runner_ipc_listener, STDIN_FILENO);
    listener.detach();

    //lua_sethook(lua.lua_state(), auto_poll_hook, LUA_MASKCOUNT, 10000);
    auto last_hotkey_poll = std::chrono::steady_clock::now(), now = std::chrono::steady_clock::now();
    // PHASE 3: Execute
    try {
        //lua.script(lua_code, "@"+filename);
        auto res = lua["_run_code_str"](lua_code, "@"+filename);

        if (!res.valid()) {
            sol::error err = res;
            // This is where your error message ("Access denied: module...") is hiding!
            send_error("Lua interpreter error: " + std::string(err.what()));
            send_exit(EXIT_FAILURE);
            return EXIT_FAILURE;
        } else {
            // If the script returned values, result[0] would have the success bool
            bool success = res[0];
            if (!success) {
                std::string msg = res[1];
                send_error("Script error: " + msg);
                send_exit(EXIT_FAILURE);
                return EXIT_FAILURE;
            }
        }

        // This loop runs if the script finishes (e.g. only contains on_key binds)
        while (!global_exit_requested) {
            now = std::chrono::steady_clock::now();
            if(now - last_hotkey_poll >= std::chrono::milliseconds(10)) {
                poll_hotkeys();
                poll_ui_events();
                last_hotkey_poll = std::chrono::steady_clock::now();
            }
            poll_frame_callback();
            lua["system"]["_update_tasks"]();
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            apply_cooperative_throttle();
        }
    } catch (const ExitError& e) {
        return global_exit_code;
    }

    send_exit(EXIT_SUCCESS);
    return EXIT_SUCCESS;
}