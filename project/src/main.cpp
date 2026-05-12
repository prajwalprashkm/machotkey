/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdatomic.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include "dialog.h"
#include "embed.h"
#include "lua_ls.h"
#include "objc_utils.h"
//#include "shared.h"
#include "shm.h"
#include "utils.h"
#include "ipc_protocol.h" // Essential for binary structs
#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <ApplicationServices/ApplicationServices.h>
#include "input_interface.h"
#include "screencapture.h"
#include "color_search.h"
#include <CoreVideo/CVDisplayLink.h>
#include "imgui.h"
#include "webview.h"
#include "nlohmann/json.hpp"
#include "httplib/httplib.h"
#include "overlay_provider.h"
#include "../include/debug_config.h"

constexpr double kDefaultMouseRatePerSec = 3000.0;
constexpr double kDefaultKeyboardRatePerSec = 3000.0;
constexpr double kDefaultKeyboardTextCharsPerSec = 131072.0;
constexpr double kHardMaxMouseRatePerSec = 100000.0;
constexpr double kHardMaxKeyboardRatePerSec = 100000.0;
constexpr double kHardMaxKeyboardTextCharsPerSec = 2000000.0;
constexpr double kDefaultCpuThrottlePercent = 75.0;
constexpr double kDefaultCpuKillPercent = 95.0;
constexpr double kMinCpuTargetPercent = 5.0;

/// Upper bound for CPU throttle/kill percentages: logical processor count × 100
/// (same scale as multi-core aggregate usage, e.g. 8 cores → 800%).
inline double max_cpu_target_percent() {
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0) {
        n = 1;
    }
    return static_cast<double>(n) * 100.0;
}
constexpr uint64_t kDefaultResidentRamLimitBytes = 4ULL * 1024 * 1024 * 1024;
constexpr uint64_t kDefaultVirtualRamLimitBytes = 32ULL * 1024 * 1024 * 1024;
constexpr uint64_t kMinResidentRamLimitBytes = 128ULL * 1024 * 1024;
constexpr uint64_t kMaxResidentRamLimitBytes = 256ULL * 1024 * 1024 * 1024;
constexpr uint64_t kMinVirtualRamLimitBytes = 512ULL * 1024 * 1024;
constexpr uint64_t kMaxVirtualRamLimitBytes = 1024ULL * 1024 * 1024 * 1024;

struct ResourceLimitConfig;
double clamp_cpu_target_percent(double value);
uint64_t clamp_resident_ram_limit_bytes(uint64_t value);
uint64_t clamp_virtual_ram_limit_bytes(uint64_t value);
ResourceLimitConfig clamp_resource_limits(const ResourceLimitConfig& in);

uint64_t initial_vram;
std::atomic<bool> vram_limit_enabled{false};
std::string macro_filename = "", current_project_dir = "";

struct RateLimitConfig {
    double mouse_events_per_sec = kDefaultMouseRatePerSec;
    double keyboard_events_per_sec = kDefaultKeyboardRatePerSec;
    double keyboard_text_chars_per_sec = kDefaultKeyboardTextCharsPerSec;
};

struct ManifestRateRequest {
    std::optional<double> mouse_events_per_sec;
    std::optional<double> keyboard_events_per_sec;
    std::optional<double> keyboard_text_chars_per_sec;
    std::optional<double> cpu_throttle_percent;
    std::optional<double> cpu_kill_percent;
    std::optional<double> resident_ram_limit_mb;
    std::optional<double> virtual_ram_limit_mb;
    std::optional<bool> virtual_ram_limit_enabled;
};

struct ResourceLimitConfig {
    double cpu_throttle_percent = kDefaultCpuThrottlePercent;
    double cpu_kill_percent = kDefaultCpuKillPercent;
    uint64_t resident_ram_limit_bytes = kDefaultResidentRamLimitBytes;
    uint64_t virtual_ram_limit_bytes = kDefaultVirtualRamLimitBytes;
    bool virtual_ram_limit_enabled = false;
};

struct ManifestPermission {
    std::string name;
    bool optional = false;
};

struct ManifestFileRule {
    std::string path;
    bool read = true;
    bool write = true;
    bool optional = false;
};

struct ProjectManifest {
    std::string raw;
    std::string entry;
    std::vector<ManifestPermission> permissions;
    std::vector<ManifestFileRule> files;
    ManifestRateRequest requested_rates;
};

ProjectManifest current_manifest;
std::unordered_map<std::string, bool> current_permission_grants;
std::atomic<bool> project_manifest_mode{false};

struct ProjectPermissionRecord {
    bool reset_on_code_change = true;
    std::string code_hash;
    std::unordered_map<std::string, bool> grants;
    RateLimitConfig approved_rates;
    ResourceLimitConfig approved_resource_limits;
};

std::atomic<double> current_mouse_rate_per_sec{kDefaultMouseRatePerSec};
std::atomic<double> current_keyboard_rate_per_sec{kDefaultKeyboardRatePerSec};
std::atomic<double> current_keyboard_text_chars_per_sec{kDefaultKeyboardTextCharsPerSec};
std::atomic<double> global_max_mouse_rate_per_sec{kDefaultMouseRatePerSec};
std::atomic<double> global_max_keyboard_rate_per_sec{kDefaultKeyboardRatePerSec};
std::atomic<double> global_max_keyboard_text_chars_per_sec{kDefaultKeyboardTextCharsPerSec};
std::atomic<double> cpu_throttle_percent{kDefaultCpuThrottlePercent};
std::atomic<double> cpu_kill_percent{kDefaultCpuKillPercent};
std::atomic<uint32_t> cpu_throttle_sleep_us{0};
std::atomic<uint64_t> resident_ram_limit_bytes{kDefaultResidentRamLimitBytes};
std::atomic<uint64_t> virtual_ram_limit_bytes{kDefaultVirtualRamLimitBytes};

nlohmann::json permission_store = nlohmann::json::object();
bool permission_store_loaded = false;

// Map keycodes to the Lua function names or IDs they should trigger
// Ensure KeyCombo has the operator< defined in utils.h as discussed
struct HotkeyData {
    bool swallow;
    bool active;
};
std::unordered_map<KeyCombo, HotkeyData> active_keybinds;
std::vector<bool> key_states(256, false);
CGEventFlags prev_flags;

ScreenCapturer capturer;
MainAppSHM main_shm;
std::atomic<bool> capturer_running = false;

std::atomic<CGRect> current_region;
std::atomic<int> current_fps;

bool lua_nextline = true;

std::atomic<pid_t> current_runner_pid = -1;
std::atomic<int> write_pipe = -1, read_pipe = -1, log_read_pipe = -1;

uint64_t current_hotkey_request_id = 1;
ResponseManager hotkey_response_manager;

struct CanvasDrawItem {
    DrawCommand cmd;
    std::vector<ImVec2> points;
};

std::vector<CanvasDrawItem> draw_commands, cached_commands;

static void drain_read_pipe_bytes(int fd, size_t n) {
    std::array<char, 8192> buf{};
    while (n > 0) {
        const size_t chunk = std::min(n, buf.size());
        const ssize_t r = read(fd, buf.data(), chunk);
        if (r <= 0) {
            return;
        }
        n -= static_cast<size_t>(r);
    }
}
std::mutex draw_command_mutex;
std::atomic<bool> new_command(false);

double screen_w, screen_h;
size_t bytes_per_row;
std::atomic<bool> bytes_per_row_ready(false);

CFMachPortRef event_tap;
CFRunLoopSourceRef run_loop_source;

long window_id = -1;

WebViewApp app;
WebViewWindow* main_win = nullptr, *overlay = nullptr;
std::unordered_map<uint32_t, WebViewWindow*> macro_ui_windows;
uint32_t macro_ui_last_window_id = 0;
int ui_server_port = -1;

LuaLSManager lua_ls_manager;

std::atomic<bool> running = true, paused = false;
std::atomic<bool> headless_mode_enabled = false;
std::chrono::steady_clock::time_point last_exceeded = std::chrono::steady_clock::now();
bool exceeded = false;

std::string log_buffer = "";

#if MHK_ENABLE_DEBUG_LOGS
#define DEBUG_LOG(msg) std::cout << "[main.cpp DEBUG]: " << msg;
#else
#define DEBUG_LOG(msg)
#endif

enum class Permission : size_t {
    LISTEN_KEYBOARD,
    CONTROL_KEYBOARD,
    CONTROL_MOUSE,
    SCREEN_CAPTURE,
    COUNT
};

std::array<std::atomic<int>, static_cast<size_t>(Permission::COUNT)> permissions;
constexpr uint32_t kMaxUiEventNameLen = 128;
constexpr uint32_t kMaxUiPayloadLen = 64 * 1024;
constexpr uint32_t kMaxUiPathLen = 1024;
constexpr uint32_t kMaxUiTitleLen = 256;
constexpr uint32_t kMaxUiRunJsLen = 256 * 1024;
constexpr uint32_t kMaxMacroUiWindows = 5;

std::string to_permission_key(Permission permission) {
    switch (permission) {
        case Permission::LISTEN_KEYBOARD:
            return "keyboard_listen";
        case Permission::CONTROL_KEYBOARD:
            return "keyboard_control";
        case Permission::CONTROL_MOUSE:
            return "mouse_control";
        case Permission::SCREEN_CAPTURE:
            return "screen_capture";
        default:
            return "";
    }
}

std::string escape_sandbox_string(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (const char c : input) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string load_file_or_empty(const std::filesystem::path& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::filesystem::path resolve_manifest_path(const std::filesystem::path& project_dir, const std::string& value) {
    const std::filesystem::path p(value);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (project_dir / p).lexically_normal();
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

bool manifest_contains_resolved_file(const std::filesystem::path& project_dir,
                                     const ProjectManifest& manifest,
                                     const std::filesystem::path& resolved_candidate) {
    const auto normalized = resolved_candidate.lexically_normal();
    if (resolve_manifest_path(project_dir, manifest.entry).lexically_normal().string() == normalized.string()) {
        return true;
    }
    for (const auto& file : manifest.files) {
        if (!file.path.empty() && file.path.back() == '/') continue;
        if (resolve_manifest_path(project_dir, file.path).lexically_normal().string() == normalized.string()) {
            return true;
        }
    }
    return false;
}

bool resolve_manifest_ui_html_path(const std::filesystem::path& project_dir,
                                   const ProjectManifest& manifest,
                                   const std::string& requested_path,
                                   std::filesystem::path& out_abs_path,
                                   std::string& error) {
    if (requested_path.empty()) {
        error = "UI path is empty";
        return false;
    }
    if (requested_path.size() > kMaxUiPathLen) {
        error = "UI path too long";
        return false;
    }

    const std::filesystem::path req(requested_path);
    if (req.is_absolute()) {
        error = "UI path must be relative to project directory";
        return false;
    }

    out_abs_path = resolve_manifest_path(project_dir, requested_path);
    if (!path_within_root(project_dir, out_abs_path)) {
        error = "UI path escapes project directory";
        return false;
    }
    if (!manifest_contains_resolved_file(project_dir, manifest, out_abs_path)) {
        error = "UI path is not listed in manifest.files";
        return false;
    }

    const std::string ext = out_abs_path.extension().string();
    if (ext != ".html" && ext != ".htm") {
        error = "UI entry must be an HTML file";
        return false;
    }

    std::ifstream in(out_abs_path);
    if (!in.is_open()) {
        error = "Unable to read UI file";
        return false;
    }
    return true;
}

bool parse_project_manifest(const std::string& raw_manifest, ProjectManifest& out_manifest, std::string& error) {
    try {
        const auto json = nlohmann::json::parse(raw_manifest);
        if (!json.is_object()) {
            error = "Manifest must be a JSON object";
            return false;
        }
        if (!json.contains("entry") || !json["entry"].is_string()) {
            error = "Manifest field 'entry' must be a string";
            return false;
        }

        ProjectManifest parsed;
        parsed.raw = raw_manifest;
        parsed.entry = json["entry"].get<std::string>();

        if (json.contains("permissions")) {
            if (!json["permissions"].is_array()) {
                error = "Manifest field 'permissions' must be an array";
                return false;
            }
            for (const auto& permission : json["permissions"]) {
                if (!permission.is_object() || !permission.contains("name") || !permission["name"].is_string()) {
                    error = "Each permission must contain a string 'name'";
                    return false;
                }
                ManifestPermission p;
                p.name = permission["name"].get<std::string>();
                if (permission.contains("optional")) {
                    p.optional = permission["optional"].get<bool>();
                }
                parsed.permissions.push_back(p);
            }
        }

        if (json.contains("files")) {
            if (!json["files"].is_array()) {
                error = "Manifest field 'files' must be an array";
                return false;
            }
            for (const auto& file : json["files"]) {
                ManifestFileRule f;
                if (file.is_string()) {
                    f.path = file.get<std::string>();
                } else if (file.is_object()) {
                    if (!file.contains("path") || !file["path"].is_string()) {
                        error = "Each object in manifest 'files' must contain a string 'path'";
                        return false;
                    }
                    f.path = file["path"].get<std::string>();
                    if (file.contains("read")) {
                        if (!file["read"].is_boolean()) {
                            error = "Manifest file field 'read' must be boolean";
                            return false;
                        }
                        f.read = file["read"].get<bool>();
                    }
                    if (file.contains("write")) {
                        if (!file["write"].is_boolean()) {
                            error = "Manifest file field 'write' must be boolean";
                            return false;
                        }
                        f.write = file["write"].get<bool>();
                    }
                    if (file.contains("optional")) {
                        if (!file["optional"].is_boolean()) {
                            error = "Manifest file field 'optional' must be boolean";
                            return false;
                        }
                        f.optional = file["optional"].get<bool>();
                    }
                    if (!f.read && !f.write) {
                        error = "Manifest file entry must enable at least one of 'read' or 'write'";
                        return false;
                    }
                } else {
                    error = "Each manifest file entry must be a string or object";
                    return false;
                }
                parsed.files.push_back(std::move(f));
            }
        }

        if (json.contains("rate_limits")) {
            if (!json["rate_limits"].is_object()) {
                error = "Manifest field 'rate_limits' must be an object";
                return false;
            }
            const auto& rates = json["rate_limits"];
            auto parse_rate = [&](const char* key, std::optional<double>& out_value) -> bool {
                if (!rates.contains(key)) return true;
                if (!rates[key].is_number()) {
                    error = std::string("Manifest rate limit '") + key + "' must be numeric";
                    return false;
                }
                const double value = rates[key].get<double>();
                if (!std::isfinite(value) || value <= 0.0) {
                    error = std::string("Manifest rate limit '") + key + "' must be > 0";
                    return false;
                }
                out_value = value;
                return true;
            };
            if (!parse_rate("mouse_events_per_sec", parsed.requested_rates.mouse_events_per_sec)) return false;
            if (!parse_rate("keyboard_events_per_sec", parsed.requested_rates.keyboard_events_per_sec)) return false;
            if (!parse_rate("keyboard_text_chars_per_sec", parsed.requested_rates.keyboard_text_chars_per_sec)) return false;
            if (!parse_rate("cpu_throttle_percent", parsed.requested_rates.cpu_throttle_percent)) return false;
            if (!parse_rate("cpu_kill_percent", parsed.requested_rates.cpu_kill_percent)) return false;
            if (!parse_rate("resident_ram_limit_mb", parsed.requested_rates.resident_ram_limit_mb)) return false;
            if (!parse_rate("virtual_ram_limit_mb", parsed.requested_rates.virtual_ram_limit_mb)) return false;
            if (rates.contains("virtual_ram_limit_enabled")) {
                if (!rates["virtual_ram_limit_enabled"].is_boolean()) {
                    error = "Manifest rate limit 'virtual_ram_limit_enabled' must be boolean";
                    return false;
                }
                parsed.requested_rates.virtual_ram_limit_enabled = rates["virtual_ram_limit_enabled"].get<bool>();
            }
        }

        out_manifest = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        error = std::string("Manifest parse error: ") + e.what();
        return false;
    }
}

std::string build_dynamic_sandbox_profile(const std::string& baseline_profile,
                                          const std::filesystem::path& project_dir,
                                          const ProjectManifest& manifest) {
    std::stringstream profile;
    profile << baseline_profile;
    profile << "\n\n;; --- Dynamic Manifest Allowlist (Main generated) ---\n";

    std::unordered_set<std::string> read_path_rules;
    std::unordered_set<std::string> write_path_rules;
    read_path_rules.insert(resolve_manifest_path(project_dir, manifest.entry).string());
    for (const auto& file : manifest.files) {
        if (!file.path.empty() && file.path.back() == '/') {
            continue; // Strict project mode requires explicit files.
        }
        const std::string resolved = resolve_manifest_path(project_dir, file.path).string();
        if (file.read) read_path_rules.insert(resolved);
        if (file.write) write_path_rules.insert(resolved);
    }

    if (!read_path_rules.empty()) {
        profile << "(allow file-read*\n";
        for (const auto& p : read_path_rules) {
            profile << "    (literal \"" << escape_sandbox_string(p) << "\")\n";
        }
        profile << ")\n";
    }

    if (!write_path_rules.empty()) {
        profile << "(allow file-write*\n";
        for (const auto& p : write_path_rules) {
            profile << "    (literal \"" << escape_sandbox_string(p) << "\")\n";
        }
        profile << ")\n";
    }
    profile << "\n;; --- Dynamic Manifest Permissions (Main generated) ---\n";
    for (const auto& permission : manifest.permissions) {
        profile << ";; permission: " << permission.name << " optional=" << (permission.optional ? "true" : "false") << "\n";
    }
    return profile.str();
}

std::filesystem::path get_permission_store_path() {
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home) : std::filesystem::temp_directory_path();
    return base / ".machotkey" / "permissions.json";
}

void ensure_permission_store_loaded() {
    if (permission_store_loaded) return;
    permission_store_loaded = true;
    const auto store_path = get_permission_store_path();
    std::ifstream in(store_path);
    if (!in.is_open()) {
        permission_store = nlohmann::json::object();
        return;
    }
    try {
        in >> permission_store;
        if (!permission_store.is_object()) {
            permission_store = nlohmann::json::object();
        }
    } catch (...) {
        permission_store = nlohmann::json::object();
    }
}

bool flush_permission_store() {
    ensure_permission_store_loaded();
    const auto store_path = get_permission_store_path();
    std::error_code ec;
    std::filesystem::create_directories(store_path.parent_path(), ec);
    std::ofstream out(store_path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << permission_store.dump(2);
    return true;
}

std::string normalize_project_key(const std::filesystem::path& project_dir) {
    return project_dir.lexically_normal().string();
}

constexpr const char* kGlobalRateSettingsKey = "__global_rate_settings__";
constexpr const char* kGlobalCpuThrottleKey = "cpu_throttle_percent";
constexpr const char* kGlobalCpuKillKey = "cpu_kill_percent";
constexpr const char* kLegacyGlobalCpuTargetKey = "cpu_target_percent";
constexpr const char* kGlobalResidentRamLimitBytesKey = "resident_ram_limit_bytes";
constexpr const char* kGlobalVirtualRamLimitBytesKey = "virtual_ram_limit_bytes";
constexpr const char* kGlobalVirtualRamEnabledKey = "virtual_ram_limit_enabled";

RateLimitConfig clamp_rate_limits(const RateLimitConfig& in) {
    RateLimitConfig out = in;
    auto clamp_or_default = [](double value, double fallback, double max_value) {
        if (!std::isfinite(value) || value <= 0.0) return fallback;
        return std::min(value, max_value);
    };
    out.mouse_events_per_sec = clamp_or_default(out.mouse_events_per_sec, kDefaultMouseRatePerSec, kHardMaxMouseRatePerSec);
    out.keyboard_events_per_sec = clamp_or_default(out.keyboard_events_per_sec, kDefaultKeyboardRatePerSec, kHardMaxKeyboardRatePerSec);
    out.keyboard_text_chars_per_sec = clamp_or_default(out.keyboard_text_chars_per_sec, kDefaultKeyboardTextCharsPerSec, kHardMaxKeyboardTextCharsPerSec);
    return out;
}

void set_global_max_rates(const RateLimitConfig& rates) {
    const auto clamped = clamp_rate_limits(rates);
    global_max_mouse_rate_per_sec.store(clamped.mouse_events_per_sec);
    global_max_keyboard_rate_per_sec.store(clamped.keyboard_events_per_sec);
    global_max_keyboard_text_chars_per_sec.store(clamped.keyboard_text_chars_per_sec);
}

RateLimitConfig get_global_max_rates() {
    return clamp_rate_limits(RateLimitConfig{
        global_max_mouse_rate_per_sec.load(),
        global_max_keyboard_rate_per_sec.load(),
        global_max_keyboard_text_chars_per_sec.load()
    });
}

RateLimitConfig apply_global_rate_ceiling(const RateLimitConfig& in) {
    const auto global = get_global_max_rates();
    RateLimitConfig out = clamp_rate_limits(in);
    out.mouse_events_per_sec = std::min(out.mouse_events_per_sec, global.mouse_events_per_sec);
    out.keyboard_events_per_sec = std::min(out.keyboard_events_per_sec, global.keyboard_events_per_sec);
    out.keyboard_text_chars_per_sec = std::min(out.keyboard_text_chars_per_sec, global.keyboard_text_chars_per_sec);
    return out;
}

nlohmann::json rate_limits_to_json(const RateLimitConfig& rates) {
    return nlohmann::json::object({
        {"mouse_events_per_sec", rates.mouse_events_per_sec},
        {"keyboard_events_per_sec", rates.keyboard_events_per_sec},
        {"keyboard_text_chars_per_sec", rates.keyboard_text_chars_per_sec}
    });
}

nlohmann::json manifest_requested_rates_to_json(const ManifestRateRequest& rates) {
    nlohmann::json out = nlohmann::json::object();
    if (rates.mouse_events_per_sec.has_value()) out["mouse_events_per_sec"] = rates.mouse_events_per_sec.value();
    if (rates.keyboard_events_per_sec.has_value()) out["keyboard_events_per_sec"] = rates.keyboard_events_per_sec.value();
    if (rates.keyboard_text_chars_per_sec.has_value()) out["keyboard_text_chars_per_sec"] = rates.keyboard_text_chars_per_sec.value();
    if (rates.cpu_throttle_percent.has_value()) out["cpu_throttle_percent"] = rates.cpu_throttle_percent.value();
    if (rates.cpu_kill_percent.has_value()) out["cpu_kill_percent"] = rates.cpu_kill_percent.value();
    if (rates.resident_ram_limit_mb.has_value()) out["resident_ram_limit_mb"] = rates.resident_ram_limit_mb.value();
    if (rates.virtual_ram_limit_mb.has_value()) out["virtual_ram_limit_mb"] = rates.virtual_ram_limit_mb.value();
    if (rates.virtual_ram_limit_enabled.has_value()) out["virtual_ram_limit_enabled"] = rates.virtual_ram_limit_enabled.value();
    return out;
}

void parse_rate_limits_from_json(const nlohmann::json& json, RateLimitConfig& out) {
    if (!json.is_object()) return;
    if (json.contains("mouse_events_per_sec") && json["mouse_events_per_sec"].is_number()) {
        out.mouse_events_per_sec = json["mouse_events_per_sec"].get<double>();
    }
    if (json.contains("keyboard_events_per_sec") && json["keyboard_events_per_sec"].is_number()) {
        out.keyboard_events_per_sec = json["keyboard_events_per_sec"].get<double>();
    }
    if (json.contains("keyboard_text_chars_per_sec") && json["keyboard_text_chars_per_sec"].is_number()) {
        out.keyboard_text_chars_per_sec = json["keyboard_text_chars_per_sec"].get<double>();
    }
    out = clamp_rate_limits(out);
}

void set_current_runtime_rates(const RateLimitConfig& rates) {
    const auto clamped = apply_global_rate_ceiling(rates);
    current_mouse_rate_per_sec.store(clamped.mouse_events_per_sec);
    current_keyboard_rate_per_sec.store(clamped.keyboard_events_per_sec);
    current_keyboard_text_chars_per_sec.store(clamped.keyboard_text_chars_per_sec);
}

RateLimitConfig negotiate_effective_rates(const std::string& macro_name,
                                          const ManifestRateRequest& requested,
                                          ProjectPermissionRecord& record) {
    record.approved_rates = apply_global_rate_ceiling(record.approved_rates);
    RateLimitConfig effective = record.approved_rates;

    struct RateField {
        const char* label;
        std::optional<double> ManifestRateRequest::* requested_member;
        double RateLimitConfig::* config_member;
    };
    const std::array<RateField, 3> fields{{
        {"mouse events/sec", &ManifestRateRequest::mouse_events_per_sec, &RateLimitConfig::mouse_events_per_sec},
        {"keyboard events/sec", &ManifestRateRequest::keyboard_events_per_sec, &RateLimitConfig::keyboard_events_per_sec},
        {"keyboard text chars/sec", &ManifestRateRequest::keyboard_text_chars_per_sec, &RateLimitConfig::keyboard_text_chars_per_sec}
    }};

    for (const auto& field : fields) {
        const auto requested_value = requested.*(field.requested_member);
        const double approved_value = record.approved_rates.*(field.config_member);
        if (!requested_value.has_value()) {
            // If not explicitly requested, use max approved rate.
            effective.*(field.config_member) = approved_value;
            continue;
        }

        const double requested_rate = requested_value.value();
        if (requested_rate <= approved_value) {
            effective.*(field.config_member) = requested_rate;
            continue;
        }

        const std::string title = "Higher Rate Requested";
        std::ostringstream desc;
        desc << macro_name << " requested " << requested_rate << " " << field.label
             << ", above approved max " << approved_value << ".\n\n"
             << "Allow Once: use requested rate for this run only.\n"
             << "Allow Always: raise approved max to this rate for future runs.\n"
             << "Keep Current: continue with approved max.";
        const auto choice = show_rate_approval_dialog(title, desc.str());
        if (choice == RateApprovalChoice::AllowOnce) {
            effective.*(field.config_member) = requested_rate;
        } else if (choice == RateApprovalChoice::AllowAlways) {
            effective.*(field.config_member) = requested_rate;
            record.approved_rates.*(field.config_member) = requested_rate;
        } else {
            effective.*(field.config_member) = approved_value;
        }
    }

    record.approved_rates = apply_global_rate_ceiling(record.approved_rates);
    return apply_global_rate_ceiling(effective);
}

ResourceLimitConfig negotiate_effective_resource_limits(const std::string& macro_name,
                                                        const ManifestRateRequest& requested,
                                                        ProjectPermissionRecord& record) {
    record.approved_resource_limits = clamp_resource_limits(record.approved_resource_limits);
    ResourceLimitConfig effective = record.approved_resource_limits;

    auto maybe_prompt_double = [&](const char* label, std::optional<double> requested_value, double& approved_value, double& effective_value) {
        if (!requested_value.has_value()) {
            effective_value = approved_value;
            return;
        }
        const double requested_limit = requested_value.value();
        if (requested_limit <= approved_value) {
            effective_value = requested_limit;
            return;
        }
        const std::string title = "Higher Limit Requested";
        std::ostringstream desc;
        desc << macro_name << " requested " << requested_limit << " " << label
             << ", above approved max " << approved_value << ".\n\n"
             << "Allow Once: use requested limit for this run only.\n"
             << "Allow Always: raise approved max to this limit for future runs.\n"
             << "Keep Current: continue with approved max.";
        const auto choice = show_rate_approval_dialog(title, desc.str());
        if (choice == RateApprovalChoice::AllowOnce) {
            effective_value = requested_limit;
        } else if (choice == RateApprovalChoice::AllowAlways) {
            effective_value = requested_limit;
            approved_value = requested_limit;
        } else {
            effective_value = approved_value;
        }
    };

    maybe_prompt_double("cpu throttle %", requested.cpu_throttle_percent,
                        record.approved_resource_limits.cpu_throttle_percent, effective.cpu_throttle_percent);
    maybe_prompt_double("cpu hard-stop %", requested.cpu_kill_percent,
                        record.approved_resource_limits.cpu_kill_percent, effective.cpu_kill_percent);

    double resident_mb_approved = static_cast<double>(record.approved_resource_limits.resident_ram_limit_bytes) / (1024.0 * 1024.0);
    double resident_mb_effective = static_cast<double>(effective.resident_ram_limit_bytes) / (1024.0 * 1024.0);
    maybe_prompt_double("resident RAM MB", requested.resident_ram_limit_mb, resident_mb_approved, resident_mb_effective);
    record.approved_resource_limits.resident_ram_limit_bytes = static_cast<uint64_t>(resident_mb_approved * 1024.0 * 1024.0);
    effective.resident_ram_limit_bytes = static_cast<uint64_t>(resident_mb_effective * 1024.0 * 1024.0);

    double virtual_mb_approved = static_cast<double>(record.approved_resource_limits.virtual_ram_limit_bytes) / (1024.0 * 1024.0);
    double virtual_mb_effective = static_cast<double>(effective.virtual_ram_limit_bytes) / (1024.0 * 1024.0);
    maybe_prompt_double("virtual RAM MB", requested.virtual_ram_limit_mb, virtual_mb_approved, virtual_mb_effective);
    record.approved_resource_limits.virtual_ram_limit_bytes = static_cast<uint64_t>(virtual_mb_approved * 1024.0 * 1024.0);
    effective.virtual_ram_limit_bytes = static_cast<uint64_t>(virtual_mb_effective * 1024.0 * 1024.0);

    if (requested.virtual_ram_limit_enabled.has_value()) {
        const bool requested_enabled = requested.virtual_ram_limit_enabled.value();
        if (requested_enabled && !record.approved_resource_limits.virtual_ram_limit_enabled) {
            const std::string title = "Virtual RAM Limit Enable Requested";
            const std::string description =
                macro_name + " requested enabling virtual RAM hard limit.\n\n"
                             "Allow Once: enable for this run only.\n"
                             "Allow Always: enable for future runs.\n"
                             "Keep Current: leave disabled.";
            const auto choice = show_rate_approval_dialog(title, description);
            if (choice == RateApprovalChoice::AllowOnce) {
                effective.virtual_ram_limit_enabled = true;
            } else if (choice == RateApprovalChoice::AllowAlways) {
                record.approved_resource_limits.virtual_ram_limit_enabled = true;
                effective.virtual_ram_limit_enabled = true;
            }
        } else {
            effective.virtual_ram_limit_enabled = requested_enabled && record.approved_resource_limits.virtual_ram_limit_enabled;
        }
    }

    DEBUG_LOG("\n\n[MAIN]: Using cpu throttle " << effective.cpu_throttle_percent << "%, cpu kill " << effective.cpu_kill_percent
              << "%, resident RAM " << effective.resident_ram_limit_bytes / (1024.0 * 1024.0) << " MB, virtual RAM "
              << effective.virtual_ram_limit_bytes / (1024.0 * 1024.0) << " MB, virtual RAM enabled " << effective.virtual_ram_limit_enabled << "\n\n\n");

    record.approved_resource_limits = clamp_resource_limits(record.approved_resource_limits);
    effective = clamp_resource_limits(effective);
    return effective;
}

bool rate_limits_equal(const RateLimitConfig& a, const RateLimitConfig& b) {
    constexpr double eps = 1e-6;
    return std::fabs(a.mouse_events_per_sec - b.mouse_events_per_sec) < eps &&
           std::fabs(a.keyboard_events_per_sec - b.keyboard_events_per_sec) < eps &&
           std::fabs(a.keyboard_text_chars_per_sec - b.keyboard_text_chars_per_sec) < eps;
}

bool resource_limits_equal(const ResourceLimitConfig& a, const ResourceLimitConfig& b) {
    constexpr double eps = 1e-6;
    return std::fabs(a.cpu_throttle_percent - b.cpu_throttle_percent) < eps &&
           std::fabs(a.cpu_kill_percent - b.cpu_kill_percent) < eps &&
           a.resident_ram_limit_bytes == b.resident_ram_limit_bytes &&
           a.virtual_ram_limit_bytes == b.virtual_ram_limit_bytes &&
           a.virtual_ram_limit_enabled == b.virtual_ram_limit_enabled;
}

// Per-project JSON can keep old approved_resource_limits while the resource panel (global atomics) was
// updated; read_project_permission_record prefers the JSON values. Merge so each run uses the stricter
// of saved project caps and current global settings—matching what get_resource_limits shows.
void merge_record_resource_limits_with_global_settings(ProjectPermissionRecord& record) {
    const ResourceLimitConfig g = clamp_resource_limits(ResourceLimitConfig{
        cpu_throttle_percent.load(),
        cpu_kill_percent.load(),
        resident_ram_limit_bytes.load(),
        virtual_ram_limit_bytes.load(),
        vram_limit_enabled.load()});
    ResourceLimitConfig& r = record.approved_resource_limits;
    r.cpu_throttle_percent = std::min(g.cpu_throttle_percent, r.cpu_throttle_percent);
    r.cpu_kill_percent = std::min(g.cpu_kill_percent, r.cpu_kill_percent);
    r.resident_ram_limit_bytes = std::min(g.resident_ram_limit_bytes, r.resident_ram_limit_bytes);
    r.virtual_ram_limit_bytes = std::min(g.virtual_ram_limit_bytes, r.virtual_ram_limit_bytes);
    r.virtual_ram_limit_enabled = g.virtual_ram_limit_enabled && r.virtual_ram_limit_enabled;
    r = clamp_resource_limits(r);
}

RateLimitConfig read_global_rate_settings() {
    ensure_permission_store_loaded();
    RateLimitConfig rates = clamp_rate_limits(RateLimitConfig{});
    if (permission_store.contains(kGlobalRateSettingsKey) && permission_store[kGlobalRateSettingsKey].is_object()) {
        const auto& entry = permission_store[kGlobalRateSettingsKey];
        if (entry.contains("max_rates")) {
            parse_rate_limits_from_json(entry["max_rates"], rates);
        }
    }
    rates = clamp_rate_limits(rates);
    set_global_max_rates(rates);
    return rates;
}

void write_global_rate_settings(const RateLimitConfig& rates) {
    ensure_permission_store_loaded();
    const auto clamped = clamp_rate_limits(rates);
    set_global_max_rates(clamped);
    permission_store[kGlobalRateSettingsKey] = nlohmann::json::object({
        {"max_rates", rate_limits_to_json(clamped)},
        {kGlobalCpuThrottleKey, clamp_cpu_target_percent(cpu_throttle_percent.load())},
        {kGlobalCpuKillKey, clamp_cpu_target_percent(cpu_kill_percent.load())},
        {kGlobalResidentRamLimitBytesKey, clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load())},
        {kGlobalVirtualRamLimitBytesKey, clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load())},
        {kGlobalVirtualRamEnabledKey, vram_limit_enabled.load()}
    });
    flush_permission_store();
}

void read_global_cpu_limit_settings() {
    ensure_permission_store_loaded();
    double throttle_target = kDefaultCpuThrottlePercent;
    double kill_target = kDefaultCpuKillPercent;
    if (permission_store.contains(kGlobalRateSettingsKey) && permission_store[kGlobalRateSettingsKey].is_object()) {
        const auto& entry = permission_store[kGlobalRateSettingsKey];
        if (entry.contains(kGlobalCpuThrottleKey) && entry[kGlobalCpuThrottleKey].is_number()) {
            throttle_target = entry[kGlobalCpuThrottleKey].get<double>();
        } else if (entry.contains(kLegacyGlobalCpuTargetKey) && entry[kLegacyGlobalCpuTargetKey].is_number()) {
            throttle_target = entry[kLegacyGlobalCpuTargetKey].get<double>();
        }
        if (entry.contains(kGlobalCpuKillKey) && entry[kGlobalCpuKillKey].is_number()) {
            kill_target = entry[kGlobalCpuKillKey].get<double>();
        } else {
            kill_target = std::max(kill_target, throttle_target);
        }
    }
    throttle_target = clamp_cpu_target_percent(throttle_target);
    kill_target = clamp_cpu_target_percent(kill_target);
    if (kill_target < throttle_target) kill_target = throttle_target;
    cpu_throttle_percent.store(throttle_target);
    cpu_kill_percent.store(kill_target);
}

void write_global_cpu_limit_settings(double throttle_target, double kill_target) {
    ensure_permission_store_loaded();
    const double throttle_clamped = clamp_cpu_target_percent(throttle_target);
    double kill_clamped = clamp_cpu_target_percent(kill_target);
    if (kill_clamped < throttle_clamped) kill_clamped = throttle_clamped;
    cpu_throttle_percent.store(throttle_clamped);
    cpu_kill_percent.store(kill_clamped);
    nlohmann::json entry = nlohmann::json::object();
    if (permission_store.contains(kGlobalRateSettingsKey) && permission_store[kGlobalRateSettingsKey].is_object()) {
        entry = permission_store[kGlobalRateSettingsKey];
    }
    if (!entry.contains("max_rates")) {
        entry["max_rates"] = rate_limits_to_json(get_global_max_rates());
    }
    entry[kGlobalCpuThrottleKey] = throttle_clamped;
    entry[kGlobalCpuKillKey] = kill_clamped;
    if (entry.contains(kLegacyGlobalCpuTargetKey)) {
        entry.erase(kLegacyGlobalCpuTargetKey);
    }
    entry[kGlobalResidentRamLimitBytesKey] = clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load());
    entry[kGlobalVirtualRamLimitBytesKey] = clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load());
    entry[kGlobalVirtualRamEnabledKey] = vram_limit_enabled.load();
    permission_store[kGlobalRateSettingsKey] = std::move(entry);
    flush_permission_store();
}

void read_global_memory_limit_settings() {
    ensure_permission_store_loaded();
    uint64_t resident_limit = kDefaultResidentRamLimitBytes;
    uint64_t virtual_limit = kDefaultVirtualRamLimitBytes;
    bool virtual_enabled = false;
    if (permission_store.contains(kGlobalRateSettingsKey) && permission_store[kGlobalRateSettingsKey].is_object()) {
        const auto& entry = permission_store[kGlobalRateSettingsKey];
        if (entry.contains(kGlobalResidentRamLimitBytesKey) && entry[kGlobalResidentRamLimitBytesKey].is_number_unsigned()) {
            resident_limit = entry[kGlobalResidentRamLimitBytesKey].get<uint64_t>();
        } else if (entry.contains(kGlobalResidentRamLimitBytesKey) && entry[kGlobalResidentRamLimitBytesKey].is_number_integer()) {
            resident_limit = static_cast<uint64_t>(std::max<int64_t>(0, entry[kGlobalResidentRamLimitBytesKey].get<int64_t>()));
        }
        if (entry.contains(kGlobalVirtualRamLimitBytesKey) && entry[kGlobalVirtualRamLimitBytesKey].is_number_unsigned()) {
            virtual_limit = entry[kGlobalVirtualRamLimitBytesKey].get<uint64_t>();
        } else if (entry.contains(kGlobalVirtualRamLimitBytesKey) && entry[kGlobalVirtualRamLimitBytesKey].is_number_integer()) {
            virtual_limit = static_cast<uint64_t>(std::max<int64_t>(0, entry[kGlobalVirtualRamLimitBytesKey].get<int64_t>()));
        }
        if (entry.contains(kGlobalVirtualRamEnabledKey) && entry[kGlobalVirtualRamEnabledKey].is_boolean()) {
            virtual_enabled = entry[kGlobalVirtualRamEnabledKey].get<bool>();
        }
    }
    resident_ram_limit_bytes.store(clamp_resident_ram_limit_bytes(resident_limit));
    virtual_ram_limit_bytes.store(clamp_virtual_ram_limit_bytes(virtual_limit));
    vram_limit_enabled.store(virtual_enabled);
}

void write_global_memory_limit_settings(uint64_t resident_limit_bytes, uint64_t virtual_limit_bytes_value, bool virtual_enabled) {
    ensure_permission_store_loaded();
    const uint64_t resident_clamped = clamp_resident_ram_limit_bytes(resident_limit_bytes);
    const uint64_t virtual_clamped = clamp_virtual_ram_limit_bytes(virtual_limit_bytes_value);
    resident_ram_limit_bytes.store(resident_clamped);
    virtual_ram_limit_bytes.store(virtual_clamped);
    vram_limit_enabled.store(virtual_enabled);

    nlohmann::json entry = nlohmann::json::object();
    if (permission_store.contains(kGlobalRateSettingsKey) && permission_store[kGlobalRateSettingsKey].is_object()) {
        entry = permission_store[kGlobalRateSettingsKey];
    }
    if (!entry.contains("max_rates")) {
        entry["max_rates"] = rate_limits_to_json(get_global_max_rates());
    }
    entry[kGlobalCpuThrottleKey] = clamp_cpu_target_percent(cpu_throttle_percent.load());
    entry[kGlobalCpuKillKey] = clamp_cpu_target_percent(cpu_kill_percent.load());
    if (entry.contains(kLegacyGlobalCpuTargetKey)) {
        entry.erase(kLegacyGlobalCpuTargetKey);
    }
    entry[kGlobalResidentRamLimitBytesKey] = resident_clamped;
    entry[kGlobalVirtualRamLimitBytesKey] = virtual_clamped;
    entry[kGlobalVirtualRamEnabledKey] = virtual_enabled;
    permission_store[kGlobalRateSettingsKey] = std::move(entry);
    flush_permission_store();
}

ProjectPermissionRecord read_project_permission_record(const std::filesystem::path& project_dir) {
    ensure_permission_store_loaded();
    ProjectPermissionRecord record;
    record.approved_resource_limits = clamp_resource_limits(ResourceLimitConfig{
        clamp_cpu_target_percent(cpu_throttle_percent.load()),
        clamp_cpu_target_percent(cpu_kill_percent.load()),
        clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load()),
        clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load()),
        vram_limit_enabled.load()
    });
    const std::string key = normalize_project_key(project_dir);
    if (!permission_store.contains(key) || !permission_store[key].is_object()) {
        return record;
    }
    const auto& entry = permission_store[key];
    record.reset_on_code_change = entry.value("reset_on_code_change", true);
    record.code_hash = entry.value("code_hash", "");
    if (entry.contains("grants") && entry["grants"].is_object()) {
        for (auto it = entry["grants"].begin(); it != entry["grants"].end(); ++it) {
            if (it.value().is_boolean()) {
                record.grants[it.key()] = it.value().get<bool>();
            }
        }
    }
    if (entry.contains("approved_rates")) {
        parse_rate_limits_from_json(entry["approved_rates"], record.approved_rates);
    }
    if (entry.contains("approved_resource_limits") && entry["approved_resource_limits"].is_object()) {
        const auto& limits = entry["approved_resource_limits"];
        if (limits.contains("cpu_throttle_percent") && limits["cpu_throttle_percent"].is_number()) {
            record.approved_resource_limits.cpu_throttle_percent = limits["cpu_throttle_percent"].get<double>();
        }
        if (limits.contains("cpu_kill_percent") && limits["cpu_kill_percent"].is_number()) {
            record.approved_resource_limits.cpu_kill_percent = limits["cpu_kill_percent"].get<double>();
        }
        if (limits.contains("resident_ram_limit_bytes") && limits["resident_ram_limit_bytes"].is_number_unsigned()) {
            record.approved_resource_limits.resident_ram_limit_bytes = limits["resident_ram_limit_bytes"].get<uint64_t>();
        }
        if (limits.contains("virtual_ram_limit_bytes") && limits["virtual_ram_limit_bytes"].is_number_unsigned()) {
            record.approved_resource_limits.virtual_ram_limit_bytes = limits["virtual_ram_limit_bytes"].get<uint64_t>();
        }
        if (limits.contains("virtual_ram_limit_enabled") && limits["virtual_ram_limit_enabled"].is_boolean()) {
            record.approved_resource_limits.virtual_ram_limit_enabled = limits["virtual_ram_limit_enabled"].get<bool>();
        }
    }
    record.approved_rates = apply_global_rate_ceiling(record.approved_rates);
    record.approved_resource_limits = clamp_resource_limits(record.approved_resource_limits);
    return record;
}

void write_project_permission_record(const std::filesystem::path& project_dir, const ProjectPermissionRecord& record) {
    ensure_permission_store_loaded();
    nlohmann::json grants = nlohmann::json::object();
    for (const auto& [name, value] : record.grants) {
        grants[name] = value;
    }
    const auto resource_limits = clamp_resource_limits(record.approved_resource_limits);
    permission_store[normalize_project_key(project_dir)] = nlohmann::json::object({
        {"reset_on_code_change", record.reset_on_code_change},
        {"code_hash", record.code_hash},
        {"grants", grants},
        {"approved_rates", rate_limits_to_json(apply_global_rate_ceiling(record.approved_rates))},
        {"approved_resource_limits", nlohmann::json::object({
            {"cpu_throttle_percent", resource_limits.cpu_throttle_percent},
            {"cpu_kill_percent", resource_limits.cpu_kill_percent},
            {"resident_ram_limit_bytes", resource_limits.resident_ram_limit_bytes},
            {"virtual_ram_limit_bytes", resource_limits.virtual_ram_limit_bytes},
            {"virtual_ram_limit_enabled", resource_limits.virtual_ram_limit_enabled}
        })}
    });
    flush_permission_store();
}

std::optional<std::string> load_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return data;
}

std::optional<std::string> compute_manifest_code_hash(const std::filesystem::path& project_dir, const ProjectManifest& manifest) {
    std::unordered_set<std::string> paths;
    paths.insert(resolve_manifest_path(project_dir, manifest.entry).string());
    for (const auto& file : manifest.files) {
        if (!file.path.empty() && file.path.back() == '/') continue;
        if (!file.read) continue;
        paths.insert(resolve_manifest_path(project_dir, file.path).string());
    }

    std::vector<std::string> sorted_paths(paths.begin(), paths.end());
    std::sort(sorted_paths.begin(), sorted_paths.end());

    uint64_t hash = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
    auto update_hash = [&](const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint8_t>(data[i]);
            hash *= 1099511628211ULL;
        }
    };

    for (const auto& p : sorted_paths) {
        auto bytes = load_file_bytes(p);
        if (!bytes.has_value()) return std::nullopt;
        update_hash(p.data(), p.size());
        const char sep = '\0';
        update_hash(&sep, 1);
        update_hash(bytes->data(), bytes->size());
        update_hash(&sep, 1);
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

std::vector<ManifestPermission> default_quick_permissions() {
    return {
        {"screen_capture", true},
        {"mouse_control", true},
        {"keyboard_control", true},
        {"keyboard_listen", true}
    };
}

std::string file_access_permission_key(const std::string& path, bool write) {
    return std::string(write ? "file_write:" : "file_read:") + path;
}

std::vector<ManifestPermission> collect_manifest_permissions(const ProjectManifest& manifest) {
    std::unordered_map<std::string, ManifestPermission> dedup;

    auto absorb = [&](const ManifestPermission& permission) {
        auto it = dedup.find(permission.name);
        if (it == dedup.end()) {
            dedup.emplace(permission.name, permission);
            return;
        }
        // If any declaration marks the permission required, treat it as required.
        it->second.optional = it->second.optional && permission.optional;
    };

    for (const auto& permission : manifest.permissions) {
        absorb(permission);
    }

    // Entry is always needed to start the macro.
    absorb({file_access_permission_key(manifest.entry, false), false});

    for (const auto& file : manifest.files) {
        if (file.path.empty() || file.path.back() == '/') continue;
        if (file.read) absorb({file_access_permission_key(file.path, false), file.optional});
        if (file.write) absorb({file_access_permission_key(file.path, true), file.optional});
    }

    std::vector<ManifestPermission> flattened;
    flattened.reserve(dedup.size());
    for (const auto& [_, permission] : dedup) {
        flattened.push_back(permission);
    }
    std::sort(flattened.begin(), flattened.end(), [](const ManifestPermission& a, const ManifestPermission& b) {
        return a.name < b.name;
    });
    return flattened;
}

std::string permission_display_name(const std::string& key) {
    constexpr const char* kReadPrefix = "file_read:";
    constexpr const char* kWritePrefix = "file_write:";
    if (key.rfind(kReadPrefix, 0) == 0) {
        return std::string("file read: ") + key.substr(std::strlen(kReadPrefix));
    }
    if (key.rfind(kWritePrefix, 0) == 0) {
        return std::string("file write: ") + key.substr(std::strlen(kWritePrefix));
    }
    return key;
}

CGEventRef listener_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon);

bool is_permission_granted(const std::unordered_map<std::string, bool>& grants, const char* key) {
    auto it = grants.find(key);
    return it != grants.end() && it->second;
}

bool can_create_keyboard_event_tap_probe() {
    const CGEventMask event_mask = CGEventMaskBit(kCGEventKeyDown) |
                                   CGEventMaskBit(kCGEventKeyUp) |
                                   CGEventMaskBit(kCGEventFlagsChanged);

    CFMachPortRef tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        event_mask,
        listener_callback,
        NULL
    );

    if (!tap) {
        return false;
    }
    CFRelease(tap);
    return true;
}

bool request_screen_capture_permission() {
    if (CGPreflightScreenCaptureAccess()) {
        return true;
    }
    if (CGRequestScreenCaptureAccess()) {
        return true;
    }
    return CGPreflightScreenCaptureAccess();
}

bool request_accessibility_permission() {
    const void* keys[] = { kAXTrustedCheckOptionPrompt };
    const void* values[] = { kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options) {
        CFRelease(options);
    }
    return trusted;
}

bool request_input_monitoring_permission() {
    if (CGPreflightListenEventAccess()) {
        return true;
    }
    if (CGRequestListenEventAccess()) {
        return true;
    }
    return CGPreflightListenEventAccess();
}

bool ensure_app_level_system_permissions(const std::string& macro_name, const std::unordered_map<std::string, bool>& grants, std::string& error_out) {
    if (is_permission_granted(grants, "screen_capture")) {
        if (!CGPreflightScreenCaptureAccess()) {
            const bool proceed = show_system_permission_dialog(
                "Screen Recording Permission Required",
                "The macro '" + macro_name + "' requires screen capture. Continue to open the native macOS permission prompt."
            );
            if (!proceed) {
                error_out = "permission request canceled: Screen Recording";
                return false;
            }
            if (!request_screen_capture_permission()) {
                error_out = "missing app-level permission: Screen Recording";
                return false;
            }
        }
    }

    if (is_permission_granted(grants, "keyboard_listen")) {
        if (!CGPreflightListenEventAccess()) {
            const bool proceed = show_system_permission_dialog(
                "Keyboard Monitoring Permission Required",
                "The macro '" + macro_name + "' listens for keyboard events. Continue to open the native macOS keyboard monitoring prompt."
            );
            if (!proceed) {
                error_out = "permission request canceled: Accessibility/Keyboard Monitoring";
                return false;
            }
            if (!request_input_monitoring_permission()) {
                error_out = "missing app-level permission: Keyboard Monitoring";
                return false;
            }
        }

        if (!can_create_keyboard_event_tap_probe()) {
            const bool proceed = show_system_permission_dialog(
                "Accessibility Permission Required",
                "The macro '" + macro_name + "' needs keyboard event tap access. Continue to open the native macOS accessibility prompt."
            );
            if (!proceed) {
                error_out = "permission request canceled: Accessibility";
                return false;
            }
            request_accessibility_permission();
            if (!can_create_keyboard_event_tap_probe()) {
                error_out = "missing app-level permission: Accessibility/Keyboard Monitoring";
                return false;
            }
        }
    }

    const bool needs_input_control_permission =
        is_permission_granted(grants, "keyboard_control") ||
        is_permission_granted(grants, "mouse_control");
    if (needs_input_control_permission && !AXIsProcessTrusted()) {
        const bool proceed = show_system_permission_dialog(
            "Input Control Permission Required",
            "The macro '" + macro_name + "' sends keyboard and/or mouse input. Continue to open the native macOS accessibility prompt."
        );
        if (!proceed) {
            error_out = "permission request canceled: Accessibility/Input Control";
            return false;
        }
        request_accessibility_permission();
        if (!AXIsProcessTrusted()) {
            error_out = "missing app-level permission: Accessibility/Input Control";
            return false;
        }
    }

    return true;
}

std::optional<std::unordered_map<std::string, bool>> prompt_for_permissions(const std::string& macro_name, const ProjectManifest& manifest) {
    std::unordered_map<std::string, bool> grants;
    for (const auto& permission : collect_manifest_permissions(manifest)) {
        const std::string request_name = permission_display_name(permission.name);
        const std::string title = permission.optional ? "Optional Permission Requested" : "Required Permission Requested";
        const std::string description = macro_name + " requests '" + request_name + "' access. " +
                                        (permission.optional ? "You can deny this and continue."
                                                             : "Denying this will stop macro startup.");
        const bool granted = show_dialog(title, description);
        grants[permission.name] = granted;
        if (!granted && !permission.optional) {
            return std::nullopt;
        }
    }
    return grants;
}

void apply_permission_grants(const std::unordered_map<std::string, bool>& grants) {
    current_permission_grants = grants;
    permissions[static_cast<size_t>(Permission::LISTEN_KEYBOARD)].store(grants.count("keyboard_listen") && grants.at("keyboard_listen") ? 1 : 0);
    permissions[static_cast<size_t>(Permission::CONTROL_KEYBOARD)].store(grants.count("keyboard_control") && grants.at("keyboard_control") ? 1 : 0);
    permissions[static_cast<size_t>(Permission::CONTROL_MOUSE)].store(grants.count("mouse_control") && grants.at("mouse_control") ? 1 : 0);
    permissions[static_cast<size_t>(Permission::SCREEN_CAPTURE)].store(grants.count("screen_capture") && grants.at("screen_capture") ? 1 : 0);
}

nlohmann::json manifest_permissions_to_json(const ProjectManifest& manifest) {
    nlohmann::json permissions_json = nlohmann::json::array();
    for (const auto& permission : collect_manifest_permissions(manifest)) {
        permissions_json.push_back(nlohmann::json::object({
            {"name", permission.name},
            {"display_name", permission_display_name(permission.name)},
            {"optional", permission.optional}
        }));
    }
    return permissions_json;
}

std::unordered_map<std::string, bool> filter_grants_to_manifest(const std::unordered_map<std::string, bool>& grants, const ProjectManifest& manifest) {
    std::unordered_map<std::string, bool> filtered;
    for (const auto& permission : collect_manifest_permissions(manifest)) {
        auto it = grants.find(permission.name);
        if (it != grants.end()) filtered[permission.name] = it->second;
    }
    return filtered;
}

std::optional<std::unordered_map<std::string, bool>> prompt_for_missing_permissions(
    const std::string& macro_name,
    const ProjectManifest& manifest,
    const std::unordered_map<std::string, bool>& existing
) {
    std::unordered_map<std::string, bool> result = existing;
    for (const auto& permission : collect_manifest_permissions(manifest)) {
        if (result.find(permission.name) != result.end()) continue;
        const std::string request_name = permission_display_name(permission.name);
        const std::string title = permission.optional ? "Optional Permission Requested" : "Required Permission Requested";
        const std::string description = macro_name + " requests '" + request_name + "' access. " +
                                        (permission.optional ? "You can deny this and continue."
                                                             : "Denying this will stop macro startup.");
        const bool granted = show_dialog(title, description);
        result[permission.name] = granted;
        if (!granted && !permission.optional) return std::nullopt;
    }
    return result;
}

bool has_denied_required_permission(const std::unordered_map<std::string, bool>& grants, const ProjectManifest& manifest) {
    for (const auto& permission : collect_manifest_permissions(manifest)) {
        if (permission.optional) continue;
        auto it = grants.find(permission.name);
        if (it != grants.end() && !it->second) return true;
    }
    return false;
}

struct QuickScriptPayload {
    std::string filename = "quick_script.lua";
    std::string code;
    std::string project_dir;
    std::vector<ManifestFileRule> files;
    std::vector<ManifestPermission> permissions;
    ManifestRateRequest requested_rates;
};

std::optional<QuickScriptPayload> parse_quick_script_payload(const std::string& data, std::string& error) {
    try {
        const auto json = nlohmann::json::parse(data);
        if (!json.is_object()) {
            error = "Invalid quick script payload";
            return std::nullopt;
        }
        QuickScriptPayload payload;
        if (json.contains("filename") && json["filename"].is_string()) {
            payload.filename = json["filename"].get<std::string>();
        }
        if (json.contains("project_dir") && json["project_dir"].is_string()) {
            payload.project_dir = json["project_dir"].get<std::string>();
        }
        if (!json.contains("code") || !json["code"].is_string()) {
            error = "Quick script payload missing 'code'";
            return std::nullopt;
        }
        payload.code = json["code"].get<std::string>();
        if (payload.code.empty()) {
            error = "Quick script is empty";
            return std::nullopt;
        }
        if (json.contains("files") && json["files"].is_array()) {
            for (const auto& file : json["files"]) {
                ManifestFileRule f;
                if (file.is_string()) {
                    f.path = file.get<std::string>();
                } else if (file.is_object() && file.contains("path") && file["path"].is_string()) {
                    f.path = file["path"].get<std::string>();
                    if (file.contains("read") && file["read"].is_boolean()) f.read = file["read"].get<bool>();
                    if (file.contains("write") && file["write"].is_boolean()) f.write = file["write"].get<bool>();
                    if (file.contains("optional") && file["optional"].is_boolean()) f.optional = file["optional"].get<bool>();
                } else {
                    continue;
                }
                if (!f.read && !f.write) continue;
                payload.files.push_back(f);
            }
        }
        if (json.contains("permissions") && json["permissions"].is_array()) {
            for (const auto& permission : json["permissions"]) {
                if (!permission.is_object() || !permission.contains("name") || !permission["name"].is_string()) continue;
                ManifestPermission p;
                p.name = permission["name"].get<std::string>();
                p.optional = permission.value("optional", true);
                payload.permissions.push_back(p);
            }
        }
        if (json.contains("rate_limits") && json["rate_limits"].is_object()) {
            const auto& rates = json["rate_limits"];
            auto parse_rate = [&](const char* key, std::optional<double>& out_value) {
                if (!rates.contains(key) || !rates[key].is_number()) return;
                const double value = rates[key].get<double>();
                if (std::isfinite(value) && value > 0.0) out_value = value;
            };
            parse_rate("mouse_events_per_sec", payload.requested_rates.mouse_events_per_sec);
            parse_rate("keyboard_events_per_sec", payload.requested_rates.keyboard_events_per_sec);
            parse_rate("keyboard_text_chars_per_sec", payload.requested_rates.keyboard_text_chars_per_sec);
            parse_rate("cpu_throttle_percent", payload.requested_rates.cpu_throttle_percent);
            parse_rate("cpu_kill_percent", payload.requested_rates.cpu_kill_percent);
            parse_rate("resident_ram_limit_mb", payload.requested_rates.resident_ram_limit_mb);
            parse_rate("virtual_ram_limit_mb", payload.requested_rates.virtual_ram_limit_mb);
            if (rates.contains("virtual_ram_limit_enabled") && rates["virtual_ram_limit_enabled"].is_boolean()) {
                payload.requested_rates.virtual_ram_limit_enabled = rates["virtual_ram_limit_enabled"].get<bool>();
            }
        }
        if (payload.permissions.empty()) {
            payload.permissions = default_quick_permissions();
        }
        payload.files.push_back(ManifestFileRule{payload.filename, true, false, false});
        return payload;
    } catch (const std::exception& e) {
        error = std::string("Invalid quick script payload: ") + e.what();
        return std::nullopt;
    }
}

int start_file_server(const std::string& data_path) {
    // 1. Initialize server
    static httplib::Server svr;

    // 2. Set mount point for your Vite 'dist' folder
    // This automatically handles MIME types (js, css, etc.)
    if (!svr.set_mount_point("/", data_path.c_str())) {
        std::cerr << "Error: dist directory not found at " << data_path << std::endl;
        return -1;
    }

    // 3. Configure for Workers/ESM (Same-Origin)
    svr.set_logger([](const auto& req, const auto& res) {
    });

    int port = svr.bind_to_any_port("127.0.0.1");
    std::thread([data_path]() {
        svr.listen_after_bind();
        DEBUG_LOG("[FILE SERVER]: Stopped." << std::endl);
    }).detach();
    return port;
}

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreVideo/CoreVideo.h>
#include <QuartzCore/QuartzCore.h>

#include <atomic>
#include <mutex>

// Intentionally never freed: detached threads may still touch these after main() returns;
// destroying std::mutex during static teardown caused libc++ "mutex lock failed: Invalid argument".
namespace {
std::mutex& get_host_ipc_mutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}
std::recursive_mutex& get_ui_lifetime_mutex() {
    static std::recursive_mutex* m = new std::recursive_mutex();
    return *m;
}
}  // namespace

static void destroy_all_macro_ui_windows_under_lock() {
    for (auto it = macro_ui_windows.begin(); it != macro_ui_windows.end(); ) {
        WebViewWindow* w = it->second;
        it = macro_ui_windows.erase(it);
        if (w) app.destroy_window(w);
    }
    macro_ui_last_window_id = 0;
}

static std::atomic<bool> g_prepare_app_terminate_done{false};

static void prepare_app_terminate() {
    if (g_prepare_app_terminate_done.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    running.store(false, std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    destroy_all_macro_ui_windows_under_lock();
    if (main_win) {
        app.destroy_window(main_win);
        main_win = nullptr;
    }
    if (overlay) {
        app.destroy_window(overlay);
        overlay = nullptr;
    }
}

inline const std::string escape_string(const std::string& str){
    return nlohmann::json(str).dump();
}

double clamp_cpu_target_percent(double value) {
    if (!std::isfinite(value)) return kDefaultCpuThrottlePercent;
    return std::max(kMinCpuTargetPercent, std::min(max_cpu_target_percent(), value));
}

uint64_t clamp_resident_ram_limit_bytes(uint64_t value) {
    return std::max(kMinResidentRamLimitBytes, std::min(kMaxResidentRamLimitBytes, value));
}

uint64_t clamp_virtual_ram_limit_bytes(uint64_t value) {
    return std::max(kMinVirtualRamLimitBytes, std::min(kMaxVirtualRamLimitBytes, value));
}

ResourceLimitConfig clamp_resource_limits(const ResourceLimitConfig& in) {
    ResourceLimitConfig out = in;
    out.cpu_throttle_percent = clamp_cpu_target_percent(out.cpu_throttle_percent);
    out.cpu_kill_percent = clamp_cpu_target_percent(out.cpu_kill_percent);
    if (out.cpu_kill_percent < out.cpu_throttle_percent) {
        out.cpu_kill_percent = out.cpu_throttle_percent;
    }
    out.resident_ram_limit_bytes = clamp_resident_ram_limit_bytes(out.resident_ram_limit_bytes);
    out.virtual_ram_limit_bytes = clamp_virtual_ram_limit_bytes(out.virtual_ram_limit_bytes);
    return out;
}

void set_runtime_resource_limits(const ResourceLimitConfig& limits) {
    const auto clamped = clamp_resource_limits(limits);
    cpu_throttle_percent.store(clamped.cpu_throttle_percent);
    cpu_kill_percent.store(clamped.cpu_kill_percent);
    resident_ram_limit_bytes.store(clamped.resident_ram_limit_bytes);
    virtual_ram_limit_bytes.store(clamped.virtual_ram_limit_bytes);
    vram_limit_enabled.store(clamped.virtual_ram_limit_enabled);
}

void send_runner_throttle_set(uint32_t sleep_us) {
    const int w = write_pipe.load();
    if (w <= 0 || current_runner_pid <= 0) return;
    ThrottlePayload payload{sleep_us};
    IPCHeader header{MsgType::SYSTEM_THROTTLE_SET, static_cast<uint32_t>(sizeof(payload)), 0};
    std::lock_guard<std::mutex> lock(get_host_ipc_mutex());
    write(w, &header, sizeof(header));
    write(w, &payload, sizeof(payload));
}

void send_runner_throttle_clear() {
    const int w = write_pipe.load();
    if (w <= 0 || current_runner_pid <= 0) return;
    IPCHeader header{MsgType::SYSTEM_THROTTLE_CLEAR, 0, 0};
    std::lock_guard<std::mutex> lock(get_host_ipc_mutex());
    write(w, &header, sizeof(header));
}

void refresh_screen_capture_excluded_windows() {
    std::vector<uint32_t> excluded_window_ids;
    if (overlay) {
        const uint32_t overlay_id = overlay->get_native_window_number();
        if (overlay_id != 0) excluded_window_ids.push_back(overlay_id);
    }
    if (main_win) {
        const uint32_t main_window_id = main_win->get_native_window_number();
        if (main_window_id != 0) excluded_window_ids.push_back(main_window_id);
    }
    set_screen_capture_excluded_window_ids(excluded_window_ids);
}

void apply_headless_mode(bool enabled) {
    headless_mode_enabled.store(enabled);
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    if (enabled) {
        if (main_win) {
            app.destroy_window(main_win);
            main_win = nullptr;
        }
        refresh_screen_capture_excluded_windows();
        return;
    }
    if (!main_win && ui_server_port > 0) {
        main_win = app.create_window("Machotkey", screen_w/4, screen_w/4, screen_w/2, screen_h/2, true, false);
        main_win->center();
        main_win->load_url("http://127.0.0.1:" + std::to_string(ui_server_port) + "/main/main.html");
    }
    refresh_screen_capture_excluded_windows();
    if (main_win) {
        main_win->show();
        main_win->bring_to_front(true);
    }
}

void emit_status_line(const std::string& text, const std::string& kind) {
    if (headless_mode_enabled.load()) return;
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    if (!main_win) return;
    main_win->send_to_js("window.addStatusLine(" + escape_string(text) + ", " + escape_string(kind) + ");");
}

void emit_console_lines(const std::string& text) {
    if (headless_mode_enabled.load()) return;
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    if (!main_win) return;
    main_win->send_to_js("window.addConsoleLines(" + escape_string(text) + ");");
}

void refresh_menu_runtime_state() {
    const bool has_project = !current_project_dir.empty() && !current_manifest.entry.empty();
    const bool macro_running = current_runner_pid.load() > 0;
    const bool macro_paused = paused.load();
    app.set_menu_runtime_state(has_project, macro_running, macro_paused);
}

std::string run_project_macro_from_state() {
    return app.invoke_binding("run_script", "");
}

std::string run_quick_script_payload(const std::string& payload) {
    return app.invoke_binding("run_quick_script", payload);
}

std::string load_project_from_dialog() {
    return app.invoke_binding("load_project", "");
}

void stop_macro_from_ui() {
    app.invoke_binding("stop_script", "");
}

void toggle_pause_resume_from_ui() {
    if (paused.load()) {
        app.invoke_binding("overlay_start_macro", "");
    } else {
        app.invoke_binding("overlay_pause_macro", "");
    }
    refresh_menu_runtime_state();
}
void on_new_frame(CVPixelBufferRef buffer) {
    if(!bytes_per_row_ready){
        bytes_per_row = CVPixelBufferGetBytesPerRow(buffer);
        bytes_per_row_ready = true;
    }
    if(!capturer_running) return;
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    
    uint8_t* src = (uint8_t*)CVPixelBufferGetBaseAddress(buffer);

    double ts = CACurrentMediaTime()*1000000;

    main_shm.write_frame(src, ts);
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

    IPCHeader header = { MsgType::SYSTEM_SCREEN_FRAME_READY, 0, 0};

    get_host_ipc_mutex().lock();
    // Send binary packet to the Runner
    write(write_pipe, &header, sizeof(header));
    get_host_ipc_mutex().unlock();

}

const uint64_t DEVICE_INDEPENDENT_FLAGS_MASK = 0xffff0000ull;

void send_response(uint64_t request_id, Response response) {
    if (read_pipe > 0) {
        IPCHeader header = { MsgType::RESPONSE, sizeof(response), request_id };

        get_host_ipc_mutex().lock();
        write(write_pipe, &header, sizeof(header));
        write(write_pipe, &response, sizeof(response));
        get_host_ipc_mutex().unlock();
    }
}

WebViewWindow* get_macro_ui_window(uint32_t window_id) {
    auto it = macro_ui_windows.find(window_id);
    if (it == macro_ui_windows.end()) return nullptr;
    return it->second;
}

void close_macro_ui_window(uint32_t window_id) {
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    auto it = macro_ui_windows.find(window_id);
    if (it == macro_ui_windows.end()) return;
    WebViewWindow* w = it->second;
    macro_ui_windows.erase(it);
    if (macro_ui_last_window_id == window_id) {
        macro_ui_last_window_id = 0;
    }
    if (w) app.destroy_window(w);
}

void close_all_macro_ui_windows() {
    std::lock_guard<std::recursive_mutex> lock(get_ui_lifetime_mutex());
    destroy_all_macro_ui_windows_under_lock();
}

void forward_ui_event_to_runner(uint32_t window_id, const std::string& event_name, const std::string& payload) {
    const int w = write_pipe.load();
    if (w <= 0 || current_runner_pid <= 0) return;
    if (event_name.empty() || event_name.size() > kMaxUiEventNameLen) return;
    if (payload.size() > kMaxUiPayloadLen) return;

    UIEventPayload meta = {
        window_id,
        static_cast<uint32_t>(event_name.size()),
        static_cast<uint32_t>(payload.size())
    };
    IPCHeader header = {
        MsgType::SYSTEM_UI_EVENT,
        static_cast<uint32_t>(sizeof(meta) + meta.event_len + meta.payload_len),
        0
    };

    std::lock_guard<std::mutex> lock(get_host_ipc_mutex());
    write(w, &header, sizeof(header));
    write(w, &meta, sizeof(meta));
    write(w, event_name.data(), meta.event_len);
    if (meta.payload_len > 0) {
        write(w, payload.data(), meta.payload_len);
    }
}
void stop_macro() {
    if (current_runner_pid > 0) {
        if (cpu_throttle_sleep_us.load(std::memory_order_relaxed) > 0) {
            send_runner_throttle_clear();
        }
        kill(current_runner_pid, SIGKILL);
        current_runner_pid = -1;
        int r = read_pipe.exchange(-1);
        int w = write_pipe.exchange(-1);
        int l = log_read_pipe.exchange(-1);

        if(r > 0) close(r);
        if(w > 0) close(w);
        if(l > 0) close(l);

        cached_commands.clear();
        draw_command_mutex.lock();
        draw_commands.clear();
        draw_command_mutex.unlock();

        if(capturer_running){
            capturer.stop();
            capturer_running = false;
        }

        for(auto& perm : permissions){
            perm.store(0);
        }
        current_permission_grants.clear();
        {
            std::lock_guard<std::recursive_mutex> ui_lock(get_ui_lifetime_mutex());
            destroy_all_macro_ui_windows_under_lock();

            if (!headless_mode_enabled.load() && main_win) {
                main_win->show();
            }

            if (overlay) {
                overlay->get_position_async([](int x, int y){
                    if (!overlay) return;
                    overlay->x = x;
                    overlay->y = y;

                    overlay->set_opacity(0.0);
                    overlay->send_to_js("window.soft_hide_taskbar();");
                    overlay->set_ignores_mouse(true);
                    overlay->set_position(0, 0);
                    overlay->set_size(screen_w, screen_h);
                });

                overlay->send_to_js("window.set_name('No macro running'); window.request_dimensions(); window.set_btn_states(true, true, true);");
            }
        }
        paused = false;
        cpu_throttle_sleep_us.store(0, std::memory_order_relaxed);
        exceeded = false;
        read_global_cpu_limit_settings();
        read_global_memory_limit_settings();
        refresh_menu_runtime_state();
    }
}

void pause_macro() {
    if (current_runner_pid > 0 && !paused.load()) {
        kill(current_runner_pid, SIGSTOP);
        paused = true;
        refresh_menu_runtime_state();
    }
}

void resume_macro() {
    if (current_runner_pid > 0 && paused.load()) {
        kill(current_runner_pid, SIGCONT);
        paused = false;
        refresh_menu_runtime_state();
    }
}

void start_listener();

CGEventRef listener_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {

    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        DEBUG_LOG("[MAIN]: Event Tap Dead. Performing Hard Reset..." << std::endl);

        if (event_tap) {
            CFMachPortInvalidate(event_tap);
            CFRelease(event_tap);
            event_tap = nullptr;
        }

        std::thread listener_thread(start_listener);
        listener_thread.detach();
        return nullptr;
    }
    if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
        // IGNORE PROGRAMMATICALLY GENERATED EVENTS
        CGEventSourceRef source = CGEventCreateSourceFromEvent(event);
        if (source) {
            CGEventSourceStateID source_state = CGEventSourceGetSourceStateID(source);
            CFRelease(source);
            
            // Only process hardware events (from actual keyboard)
            if (source_state != kCGEventSourceStateHIDSystemState) {
                return event;  // Ignore synthetic events
            }
        }
        
        uint16_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        CGEventFlags flags = CGEventGetFlags(event) & DEVICE_INDEPENDENT_FLAGS_MASK;

        //DEBUG_LOG("[MAIN]: Event Type: " << type << ", Keycode: " << keycode << ", Flags: " << flags << std::endl);

        // Emergency Kill (Cmd + Esc)
        if (keycode == 53 && (flags & kCGEventFlagMaskCommand)) {
            if(current_runner_pid != -1) DEBUG_LOG("[MAIN]: Emergency Stop Triggered. Killing Runner (PID: " << current_runner_pid << ")" << std::endl);
            stop_macro();
            return event;
        }

        // UPDATE KEY STATES FIRST
        if(type == kCGEventKeyDown){
            key_states[keycode] = true;
        }else if (type == kCGEventKeyUp){
            key_states[keycode] = false;
        }else{
            prev_flags = flags;
        }
        
        // --- Binary IPC Trigger ---
        KeyCombo current;
        current.count = 0;
        for(uint16_t code = 0; code < key_states.size(); code++){
            if(current.count >= 16) break; // Max keys reached
            if(key_states[code]){
                current.keycodes[current.count++] = code;
            }
        }

        current.flags = flags;
        normalize_combo(current);

        bool found = active_keybinds.count(current);

        if(type == kCGEventKeyDown && CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) && found){
            // Ignore auto-repeats for registered keybinds
            return event;
        }

        if (type == kCGEventKeyDown && found && !active_keybinds[current].active && !paused.load()) {
            DEBUG_LOG("[MAIN]: Hotkey Triggered (Keycode: " << current.keycodes[0] 
                      << ", Flags: " << current.flags << ", Count: " << (int) current.count << ")" << std::endl);

            IPCHeader header = { MsgType::TRIGGER_EVENT, sizeof(KeyCombo), current_hotkey_request_id};

            // Send binary packet to the Runner
            get_host_ipc_mutex().lock();
            write(write_pipe, &header, sizeof(header));
            write(write_pipe, &current, sizeof(current));
            get_host_ipc_mutex().unlock();

            active_keybinds[current].active = true; // Mark as triggered
            
            return active_keybinds[current].swallow ? NULL : event;
        }

        // Handle key release - deactivate any combos that included this key
        if (type == kCGEventKeyUp) {
            for (auto& [combo, data] : active_keybinds) {
                if (data.active) {
                    // Check if any key in the combo is no longer held
                    bool still_held = true;
                    for(int i = 0; i < combo.count; ++i) {
                        if (!key_states[combo.keycodes[i]]) {
                            still_held = false;
                            break;
                        }
                    }
                    if (!still_held) {
                        data.active = false;
                    }
                }
            }
        }
    }

    return event;
}

void start_listener() {
    CGEventMask event_mask = CGEventMaskBit(kCGEventKeyDown) |
                            CGEventMaskBit(kCGEventKeyUp) |
                            CGEventMaskBit(kCGEventFlagsChanged) //|
                            /*CGEventMaskBit(kCGEventMouseMoved)*/;

    event_tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        event_mask,
        listener_callback,
        NULL
    );

    if (!event_tap) {
        std::cerr << "Fatal: Accessibility Permissions required!" << std::endl;
        return;
    }

    run_loop_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source, kCFRunLoopCommonModes);
    CGEventTapEnable(event_tap, true);

    CFRunLoopRun();
}
#endif

uint64_t last_seq = 0;
auto last_change = std::chrono::steady_clock::now();

void start_screencapture_watchdog(ScreenCapturer& capturer, MainAppSHM& shm, std::atomic<CGRect>& region, std::atomic<int>& fps) {
    std::thread([&capturer, &shm, &region, &fps]() {
        double last_captured_ts = -1.0; 
        auto last_activity = std::chrono::steady_clock::now();

        while (running.load(std::memory_order_relaxed)) {
            // Sleep - check once per second
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Only monitor if we are supposed to be active
            if (capturer_running && shm.header != nullptr) {
                double current_ts = shm.header->capture_timestamp;

                // 1. Initial Sync: If we haven't recorded a TS yet, grab the current one and wait.
                if (last_captured_ts < 0) {
                    last_captured_ts = current_ts;
                    last_activity = std::chrono::steady_clock::now();
                    continue;
                }

                // 2. High-precision comparison
                // Since you *1000000, the difference should be large.
                if (std::abs(current_ts - last_captured_ts) > 0.001) {
                    last_captured_ts = current_ts;
                    last_activity = std::chrono::steady_clock::now();
                } else {
                    // 3. Stale data check
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity).count();

                    if (elapsed >= 5) { // 5 second grace period
                        std::cerr << "[WATCHDOG] STALL: TS stayed at " << current_ts << " for 5s. Restarting..." << std::endl;
                        
                        capturer.stop();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        
                        CGRect rg = region.load();
                        int fp = fps.load();

                        bool res = CGRectIsNull(rg) ? capturer.start(fp) : capturer.start_region(rg, on_new_frame, fp);

                        if (res) {
                            std::cout << "[WATCHDOG] Recovery successful." << std::endl;
                            // Critical: Reset these so we don't immediately trigger again
                            last_captured_ts = shm.header->capture_timestamp;
                            last_activity = std::chrono::steady_clock::now();
                        }
                    }
                }
            } else {
                // If not running, keep resetting the timestamp tracker
                last_captured_ts = -1.0;
                last_activity = std::chrono::steady_clock::now();
            }
        }
    }).detach();
}

// main.cpp
void monitor_data() {
    auto has_permission = [](Permission permission) -> bool {
        return permissions[static_cast<size_t>(permission)].load() != 0;
    };
    struct TokenBucketLimiter {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;

        explicit TokenBucketLimiter(double initial_tokens)
            : tokens(initial_tokens),
              last_refill(std::chrono::steady_clock::now()) {}

        bool allow(double burst, double refill_per_second, double cost = 1.0) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - last_refill).count();
            last_refill = now;
            tokens = std::min(burst, tokens + elapsed * refill_per_second);
            if (tokens >= cost) {
                tokens -= cost;
                return true;
            }
            return false;
        }
    };
    TokenBucketLimiter mouse_limiter(kDefaultMouseRatePerSec);
    TokenBucketLimiter keyboard_limiter(kDefaultKeyboardRatePerSec);
    TokenBucketLimiter keyboard_text_limiter(kDefaultKeyboardTextCharsPerSec);
    auto last_throttle_log = std::chrono::steady_clock::now();
    auto maybe_log_throttle = [&](const char* kind) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_throttle_log > std::chrono::milliseconds(500)) {
            DEBUG_LOG("[MAIN]: Throttled excessive " << kind << " requests from runner." << std::endl);
            last_throttle_log = now;
        }
    };

    // read_pipe is the handle to the read end of Pipe B (fd 3 in child)
    while(running.load(std::memory_order_relaxed)){
        if (read_pipe == -1){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        while (read_pipe > 0) {
            IPCHeader header;
            if (read(read_pipe, &header, sizeof(header)) <= 0) break;

            //std::cout << "[MAIN]: Received message of type " << (int)header.type << " with payload size " << header.payload_size << " and request ID " << header.request_id << std::endl;
            //DEBUG_LOG("[MAIN]: Is DRAW_COMMAND: " << (header.type == MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND) << std::endl);
            /*if (header.type == MsgType::RESPONSE) {
                Response resp;
                if (read(read_pipe, &resp, sizeof(resp)) > 0) {
                    hotkey_response_manager.fulfill_response(header.request_id, resp);
                }
            }else */
            
            //DEBUG_LOG("Received IPC header for id: " << header.request_id)
            if(header.type == MsgType::EXIT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if(current_runner_pid != -1) DEBUG_LOG("[MAIN]: Macro exit requested. Runner exiting (PID: " << current_runner_pid << ")" << std::endl);
                stop_macro();
                if(header.payload_size == EXIT_SUCCESS) emit_status_line("✓ Macro execution complete!", "success");
                else emit_status_line("✗ Macro execution stopped with errors.", "error");
            }else if(header.type == MsgType::ERROR){
                uint32_t msg_len = header.payload_size;
                std::vector<char> buf(msg_len + 1);
                if (read(read_pipe, buf.data(), msg_len) > 0) {
                    buf[msg_len] = '\0';
                    std::string error_msg(buf.data());
                    DEBUG_LOG("[RUNNER ERROR]: " << error_msg << std::endl);
                    emit_status_line("✗ Error: " + error_msg, "error");
                }
            }else if (header.type == MsgType::ADD_KEYBIND) {
                KeyCombo combo;
                if (read(read_pipe, &combo, sizeof(combo)) > 0) {
                    if(!has_permission(Permission::LISTEN_KEYBOARD)){
                        DEBUG_LOG("[MAIN]: Keyboard monitoring not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }

                    normalize_combo(combo);
                    // Add to the map used by the CGEventTap callback
                    active_keybinds[combo] = {combo.swallow, false};
                    
                    DEBUG_LOG("[MAIN]: Registered new keybind (Keycode: " << combo.keycodes[0] 
                            << ", Flags: " << combo.flags << ", Count: " << (int) combo.count << ")" << std::endl);
                    
                    send_response(header.request_id, { true });
                }
            }else if (header.type == MsgType::REM_KEYBIND) {
                KeyCombo combo;
                if (read(read_pipe, &combo, sizeof(combo)) > 0) {
                    normalize_combo(combo);
                    // Remove from the map used by the CGEventTap callback
                    active_keybinds.erase(combo);
                    
                    DEBUG_LOG("[MAIN]: Removed keybind (Keycode: " << combo.keycodes[0] 
                            << ", Flags: " << combo.flags << ", Count: " << (int) combo.count << ")" << std::endl);

                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_MOUSE_MOVE){
                MouseData md;
                if(read(read_pipe, &md, sizeof(md)) > 0){
                    if(!has_permission(Permission::CONTROL_MOUSE)){
                        DEBUG_LOG("[MAIN]: Mouse control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    const double mouse_rate = std::max(1.0, current_mouse_rate_per_sec.load());
                    if(!mouse_limiter.allow(std::max(32.0, mouse_rate * 0.25), mouse_rate, 1.0)){
                        maybe_log_throttle("mouse");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    //DEBUG_LOG("[MAIN]: Received mouse move command to (" << md.x << ", " << md.y << ") over " << md.duration << " seconds." << std::endl);
                    InputInterface::Mouse::move(md.x, md.y, md.duration);

                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_MOUSE_CLICK){
                MouseData md;
                if(read(read_pipe, &md, sizeof(md)) > 0){
                    if(!has_permission(Permission::CONTROL_MOUSE)){
                        DEBUG_LOG("[MAIN]: Mouse control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    const double mouse_rate = std::max(1.0, current_mouse_rate_per_sec.load());
                    if(!mouse_limiter.allow(std::max(32.0, mouse_rate * 0.25), mouse_rate, std::max(1, md.clicks))){
                        maybe_log_throttle("mouse");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    //DEBUG_LOG("[MAIN]: Received mouse click command at (" << md.x << ", " << md.y << ") button " << (int)md.button << " for " << md.clicks << " clicks." << std::endl);
                    if(md.x >= 0 && md.y >= 0){
                        InputInterface::Mouse::click(md.x, md.y, static_cast<InputInterface::Mouse::MouseButton>(md.button), md.clicks);
                    }else{
                        InputInterface::Mouse::click(static_cast<InputInterface::Mouse::MouseButton>(md.button), md.clicks);
                    }

                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_MOUSE_EVENT){
                MouseData md;
                if(read(read_pipe, &md, sizeof(md)) > 0){
                    if(!has_permission(Permission::CONTROL_MOUSE)){
                        DEBUG_LOG("[MAIN]: Mouse control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    const double mouse_rate = std::max(1.0, current_mouse_rate_per_sec.load());
                    if(!mouse_limiter.allow(std::max(32.0, mouse_rate * 0.25), mouse_rate, 1.0)){
                        maybe_log_throttle("mouse");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    //DEBUG_LOG("[MAIN]: Received mouse event command at (" << md.x << ", " << md.y << ") button " << (int)md.button << " event type " << (int)md.event_type << "." << std::endl);
                    if(md.x >= 0 && md.y >= 0){
                        InputInterface::Mouse::send_event(md.x, md.y, static_cast<InputInterface::Mouse::MouseButton>(md.button), static_cast<InputInterface::Mouse::MouseEventType>(md.event_type));
                    }else{
                        auto current_pos = InputInterface::Mouse::get_pos();
                        InputInterface::Mouse::send_event(current_pos.x, current_pos.y, static_cast<InputInterface::Mouse::MouseButton>(md.button), static_cast<InputInterface::Mouse::MouseEventType>(md.event_type));
                    }

                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_MOUSE_REQUEST_POSITION){
                if(!has_permission(Permission::CONTROL_MOUSE)){
                    DEBUG_LOG("[MAIN]: Mouse control permission not granted. Ignoring request." << std::endl);
                    send_response(header.request_id, { false });
                    continue;
                }

                auto pos = InputInterface::Mouse::get_pos();
                MousePosition response = {pos.x, pos.y};

                IPCHeader send_header = { MsgType::SYSTEM_MOUSE_FULFILL_POSITION, sizeof(response),  header.request_id};

                get_host_ipc_mutex().lock();
                write(write_pipe, &send_header, sizeof(send_header));
                write(write_pipe, &response, sizeof(response));
                get_host_ipc_mutex().unlock();
            }if(header.type == MsgType::SYSTEM_KEYBOARD_PRESS){
                KeyboardData kd;
                if(read(read_pipe, &kd, sizeof(kd)) > 0){
                    if(!has_permission(Permission::CONTROL_KEYBOARD)){
                        DEBUG_LOG("[MAIN]: Keyboard control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    const double keyboard_rate = std::max(1.0, current_keyboard_rate_per_sec.load());
                    if(!keyboard_limiter.allow(std::max(32.0, keyboard_rate * 0.25), keyboard_rate, 1.0)){
                        maybe_log_throttle("keyboard");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    //DEBUG_LOG("[MAIN]: Received keyboard press command for keycode " << kd.keycode << " event type " << (int)kd.event_type << "." << std::endl);
                    InputInterface::Keyboard::press(kd.keycode, kd.flags);
                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_KEYBOARD_EVENT){
                KeyboardData kd;
                if(read(read_pipe, &kd, sizeof(kd)) > 0){
                    if(!has_permission(Permission::CONTROL_KEYBOARD)){
                        DEBUG_LOG("[MAIN]: Keyboard control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    const double keyboard_rate = std::max(1.0, current_keyboard_rate_per_sec.load());
                    if(!keyboard_limiter.allow(std::max(32.0, keyboard_rate * 0.25), keyboard_rate, 1.0)){
                        maybe_log_throttle("keyboard");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    //DEBUG_LOG("[MAIN]: Received keyboard event command for keycode " << kd.keycode << " event type " << (int)kd.event_type << "." << std::endl);
                    InputInterface::Keyboard::send_event(kd.keycode, kd.flags, static_cast<InputInterface::Keyboard::KeyEventType>(kd.event_type));
                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_KEYBOARD_TYPE){
                InputInterface::Keyboard::TypeMode mode;
                if(read(read_pipe, &mode, sizeof(mode)) > 0){
                    if(!has_permission(Permission::CONTROL_KEYBOARD)){
                        DEBUG_LOG("[MAIN]: Keyboard control permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }
                    int interval_ms;
                    read(read_pipe, &interval_ms, sizeof(interval_ms));

                    if (header.payload_size < sizeof(mode) + sizeof(interval_ms)) {
                        send_response(header.request_id, { false });
                        continue;
                    }
                    // 3. Read the Remaining String
                    uint32_t string_len = header.payload_size - static_cast<uint32_t>(sizeof(mode) + sizeof(interval_ms));
                    constexpr uint32_t kMaxKeyboardTypePayload = 256 * 1024;
                    if (string_len > kMaxKeyboardTypePayload) {
                        std::vector<char> discard(string_len);
                        read(read_pipe, discard.data(), string_len);
                        maybe_log_throttle("keyboard text");
                        send_response(header.request_id, { false });
                        continue;
                    }
                    std::vector<char> buf(string_len + 1);
                    read(read_pipe, buf.data(), string_len);
                    buf[string_len] = '\0';

                    std::string received_text(buf.data());
                    const double text_rate = std::max(16.0, current_keyboard_text_chars_per_sec.load());
                    if(!keyboard_text_limiter.allow(std::max(128.0, text_rate * 0.25), text_rate, std::max<size_t>(1, received_text.size()))){
                        maybe_log_throttle("keyboard text");
                        send_response(header.request_id, { false });
                        continue;
                    }

                    //DEBUG_LOG("[MAIN]: Received keyboard type command with mode " << (int)mode << " text: " << received_text << std::endl);
                    InputInterface::Keyboard::type(received_text, mode, interval_ms);

                    //DEBUG_LOG("done typing" << std::endl);
                    send_response(header.request_id, { true });
                }
            }else if(header.type == MsgType::SYSTEM_SCREEN_REQUEST_DIM){
                DEBUG_LOG("[MAIN]: Received screen dimension request." << std::endl);
                double w, h;
                get_screen_dim(w, h);

                ScreenDim dim = {w, h};

                IPCHeader send_header = { MsgType::RESPONSE, sizeof(dim),  header.request_id};

                get_host_ipc_mutex().lock();
                write(write_pipe, &send_header, sizeof(send_header));
                write(write_pipe, &dim, sizeof(dim));
                get_host_ipc_mutex().unlock();
            }else if(header.type == MsgType::SYSTEM_SCREEN_START_CAPTURE){
                ScreenCaptureRequest req;
                if(read(read_pipe, &req, sizeof(req)) > 0){
                    DEBUG_LOG("[MAIN]: Received screen capture start command for region (" << req.x << ", " << req.y << ", " << req.w << ", " << req.h << ") at " << req.fps << " FPS." << std::endl);

                    if(!has_permission(Permission::SCREEN_CAPTURE)){
                        DEBUG_LOG("[MAIN]: Screen capture permission not granted. Ignoring request." << std::endl);
                        send_response(header.request_id, { false });
                        continue;
                    }


                    if(capturer_running) capturer.stop();
                    capturer_running = false;

                    auto shm_name = "/macro_buffer_shm"+std::to_string(current_runner_pid);
                    if(req.w == 0 || req.h == 0){
                        double w, h;
                        get_screen_dim(w, h);

                        req.w = static_cast<size_t>(w);
                        req.h = static_cast<size_t>(h);
                        req.x = 0;
                        req.y = 0;

                        bytes_per_row_ready = false;

                        current_region = CGRectNull;
                        current_fps = req.fps;

                        capturer.start(on_new_frame, req.fps, req.display_id);
                    }else{
                        bytes_per_row_ready = false;

                        current_region = CGRectMake(req.x, req.y, req.w, req.h);
                        current_fps = req.fps;

                        capturer.start_region(CGRectMake(req.x, req.y, req.w, req.h), on_new_frame, req.fps, req.display_id);
                    }

                    //capturer.get_next_frame(&frameData, req.w, req.h, bytes_per_row);

                    while(!bytes_per_row_ready){
                        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                    }

                    main_shm.init(shm_name, req.w, req.h, bytes_per_row);
                    capturer_running = true;

                    ScreenMetadata metadata;
                    metadata.width = req.w;
                    metadata.height = req.h;
                    metadata.stride = bytes_per_row;
                    std::strncpy(metadata.shm_name, shm_name.c_str(), sizeof(metadata.shm_name));

                    IPCHeader send_header = { MsgType::SYSTEM_SCREEN_METADATA, sizeof(metadata),  header.request_id};

                    send_response(header.request_id, { true });

                    get_host_ipc_mutex().lock();
                    write(write_pipe, &send_header, sizeof(send_header));
                    write(write_pipe, &metadata, sizeof(metadata));
                    get_host_ipc_mutex().unlock();
                }
            }else if(header.type == MsgType::SYSTEM_SCREEN_STOP_CAPTURE){
                DEBUG_LOG("[MAIN]: Received screen capture stop command." << std::endl);
                if(capturer_running){
                    capturer.stop();
                    capturer_running = false;
                    main_shm.cleanup();
                    main_shm.remove_shm();
                }
            }else if(header.type == MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND){
                if (header.payload_size < sizeof(DrawCommand)) {
                    continue;
                }
                DrawCommand cmd{};
                if (read(read_pipe, &cmd, sizeof(cmd)) <= 0) {
                    break;
                }
                cmd.data = header.request_id;

                const size_t extra = static_cast<size_t>(header.payload_size) - sizeof(DrawCommand);
                std::vector<ImVec2> poly;
                if (extra > kMaxCanvasPolyExtraBytes || (extra % (2 * sizeof(float))) != 0) {
                    drain_read_pipe_bytes(read_pipe, extra);
                    continue;
                }
                if (extra > 0) {
                    std::vector<float> raw(extra / sizeof(float));
                    if (read(read_pipe, raw.data(), extra) <= 0) {
                        break;
                    }
                    poly.reserve(raw.size() / 2);
                    for (size_t i = 0; i + 1 < raw.size(); i += 2) {
                        poly.emplace_back(raw[i], raw[i + 1]);
                    }
                }

                if (cmd.type == DrawCmdType::CLEAR) {
                    cached_commands.clear();
                    draw_command_mutex.lock();
                    draw_commands.clear();
                    draw_command_mutex.unlock();
                    send_response(cmd.data, {true});
                    new_command = true;
                    continue;
                }

                bool geom_ok = true;
                if (draw_cmd_uses_point_payload(cmd.type)) {
                    if (cmd.type == DrawCmdType::POLYGON && poly.size() < 3) {
                        geom_ok = false;
                    } else if (cmd.type == DrawCmdType::POLYLINE && poly.size() < 2) {
                        geom_ok = false;
                    } else if (cmd.type == DrawCmdType::QUADRATIC_CURVE && poly.size() != 3) {
                        geom_ok = false;
                    } else if (cmd.type == DrawCmdType::BEZIER_CURVE && poly.size() != 4) {
                        geom_ok = false;
                    }
                } else if (extra > 0) {
                    geom_ok = false;
                }
                if (!geom_ok) {
                    continue;
                }

                bool existing = false;
                int i = 0;
                bool did_unlock = false;
                draw_command_mutex.lock();
                const auto id_str = std::string(cmd.id);
                const bool searching_id = id_str != "";
                const bool searching_class = cmd.class_count > 0;
                if (searching_id) {
                    for (auto& existing_item : draw_commands) {
                        if (std::string(existing_item.cmd.id) == id_str) {
                            if (cmd.type == DrawCmdType::REMOVE) {
                                draw_commands.erase(draw_commands.begin() + i);
                                draw_command_mutex.unlock();
                                did_unlock = true;
                                existing = true;
                                break;
                            }
                            existing_item.cmd = cmd;
                            existing_item.points = std::move(poly);
                            draw_command_mutex.unlock();
                            did_unlock = true;
                            existing = true;
                            break;
                        }
                        i += 1;
                    }
                } else if (searching_class && cmd.type == DrawCmdType::REMOVE) {
                    std::vector<int> to_remove;
                    for (auto& existing_item : draw_commands) {
                        bool remove_cmd = false;
                        for (uint32_t c = 0; c < cmd.class_count; c++) {
                            remove_cmd = existing_item.cmd.has_class(cmd.classes[c]);
                            if (!remove_cmd) {
                                break;
                            }
                        }
                        if (remove_cmd) {
                            to_remove.push_back(i);
                        }
                        i += 1;
                    }
                    for (int idx = static_cast<int>(to_remove.size()) - 1; idx >= 0; idx--) {
                        draw_commands.erase(draw_commands.begin() + to_remove[idx]);
                    }
                    draw_command_mutex.unlock();
                    did_unlock = true;
                }
                if (!did_unlock) {
                    if (!existing && cmd.type != DrawCmdType::REMOVE) {
                        draw_commands.push_back(CanvasDrawItem{cmd, std::move(poly)});
                    }
                    draw_command_mutex.unlock();
                }
                new_command = true;
            }else if(header.type == MsgType::SYSTEM_SCREEN_CANVAS_SET_DISPLAY){
                MoveOverlayToDisplay(header.payload_size);
                send_response(header.request_id, {true});
            } else if (header.type == MsgType::SYSTEM_UI_OPEN) {
                if (header.payload_size < sizeof(UIOpenPayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }

                UIOpenPayload meta{};
                if (read(read_pipe, &meta, sizeof(meta)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                if (meta.window_id == 0 ||
                    meta.path_len == 0 ||
                    meta.path_len > kMaxUiPathLen ||
                    meta.title_len > kMaxUiTitleLen ||
                    header.payload_size != sizeof(meta) + meta.path_len + meta.title_len) {
                    const size_t discard_len = static_cast<size_t>(meta.path_len) + static_cast<size_t>(meta.title_len);
                    if (discard_len > 0 && discard_len <= 4 * 1024 * 1024) {
                        std::vector<char> discard(discard_len);
                        read(read_pipe, discard.data(), discard_len);
                    }
                    send_response(header.request_id, {false});
                    continue;
                }

                std::vector<char> buf(meta.path_len + 1);
                if (read(read_pipe, buf.data(), meta.path_len) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                buf[meta.path_len] = '\0';
                const std::string requested_path(buf.data());
                std::string window_title = "Macro UI";
                if (meta.title_len > 0) {
                    std::vector<char> title_buf(meta.title_len + 1);
                    if (read(read_pipe, title_buf.data(), meta.title_len) <= 0) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    title_buf[meta.title_len] = '\0';
                    window_title = std::string(title_buf.data());
                }

                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    if (current_project_dir.empty()) {
                        emit_status_line("✗ Cannot open UI: no project loaded", "error");
                        send_response(header.request_id, {false});
                        continue;
                    }
                    if (macro_ui_windows.size() >= kMaxMacroUiWindows) {
                        emit_status_line("✗ Cannot open UI: window limit reached", "error");
                        send_response(header.request_id, {false});
                        continue;
                    }
                    if (macro_ui_windows.count(meta.window_id) > 0) {
                        emit_status_line("✗ Cannot open UI: duplicate window id", "error");
                        send_response(header.request_id, {false});
                        continue;
                    }

                    std::filesystem::path ui_abs;
                    std::string error;
                    if (!resolve_manifest_ui_html_path(std::filesystem::path(current_project_dir), current_manifest, requested_path, ui_abs, error)) {
                        emit_status_line("✗ UI open blocked: " + error, "error");
                        send_response(header.request_id, {false});
                        continue;
                    }

                    int x = static_cast<int>(screen_w / 3);
                    int y = static_cast<int>(screen_h / 5);
                    int w = static_cast<int>(screen_w / 3);
                    int h = static_cast<int>(screen_h / 2);
                    if (meta.flags & kUiOpenFlagHasX) x = meta.x;
                    if (meta.flags & kUiOpenFlagHasY) y = meta.y;
                    if (meta.flags & kUiOpenFlagHasWidth) w = meta.width;
                    if (meta.flags & kUiOpenFlagHasHeight) h = meta.height;
                    if (w <= 0 || h <= 0) {
                        send_response(header.request_id, {false});
                        continue;
                    }

                    WebViewWindow* created = app.create_window(window_title, x, y, w, h, true, false);
                    if (!created) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    const uint32_t source_window_id = meta.window_id;
                    created->set_macro_ui_handler([source_window_id](const std::string& event_name, const std::string& payload) {
                        forward_ui_event_to_runner(source_window_id, event_name, payload);
                    });
                    created->load_project_html_file(ui_abs.string(), current_project_dir);
                    macro_ui_windows[source_window_id] = created;
                    macro_ui_last_window_id = source_window_id;
                    send_response(header.request_id, {true});
                }
            } else if (header.type == MsgType::SYSTEM_UI_CLOSE) {
                uint32_t target_id = macro_ui_last_window_id;
                if (header.payload_size == sizeof(UITargetPayload)) {
                    UITargetPayload payload{};
                    if (read(read_pipe, &payload, sizeof(payload)) <= 0) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target_id = payload.window_id;
                } else if (header.payload_size > 0 && header.payload_size <= 4 * 1024 * 1024) {
                    std::vector<char> discard(header.payload_size);
                    read(read_pipe, discard.data(), header.payload_size);
                    send_response(header.request_id, {false});
                    continue;
                }
                if (target_id != 0 && get_macro_ui_window(target_id)) {
                    close_macro_ui_window(target_id);
                    send_response(header.request_id, {true});
                } else {
                    send_response(header.request_id, {false});
                }
            } else if (header.type == MsgType::SYSTEM_UI_RUN_JS) {
                uint32_t target_id = macro_ui_last_window_id;
                uint32_t js_len = header.payload_size;
                if (header.payload_size >= sizeof(UITargetStringPayload)) {
                    UITargetStringPayload meta{};
                    if (read(read_pipe, &meta, sizeof(meta)) <= 0) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target_id = meta.window_id;
                    js_len = meta.data_len;
                    if (header.payload_size != sizeof(meta) + js_len) {
                        if (js_len <= 4 * 1024 * 1024) {
                            std::vector<char> discard(js_len);
                            read(read_pipe, discard.data(), js_len);
                        }
                        send_response(header.request_id, {false});
                        continue;
                    }
                }
                if (js_len == 0 || js_len > kMaxUiRunJsLen) {
                    if (js_len > kMaxUiRunJsLen && js_len <= 4 * 1024 * 1024) {
                        std::vector<char> discard(js_len);
                        read(read_pipe, discard.data(), js_len);
                    }
                    send_response(header.request_id, {false});
                    continue;
                }
                std::vector<char> buf(js_len + 1);
                if (read(read_pipe, buf.data(), js_len) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                buf[js_len] = '\0';
                const std::string js(buf.data());
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(target_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target->send_to_js(js);
                }
                send_response(header.request_id, {true});
            } else if (header.type == MsgType::SYSTEM_UI_SET_TITLE) {
                if (header.payload_size < sizeof(UITargetStringPayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }
                UITargetStringPayload meta{};
                if (read(read_pipe, &meta, sizeof(meta)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                if (meta.data_len == 0 || meta.data_len > kMaxUiTitleLen || header.payload_size != sizeof(meta) + meta.data_len) {
                    if (meta.data_len > 0 && meta.data_len <= 4 * 1024 * 1024) {
                        std::vector<char> discard(meta.data_len);
                        read(read_pipe, discard.data(), meta.data_len);
                    }
                    send_response(header.request_id, {false});
                    continue;
                }
                std::vector<char> buf(meta.data_len + 1);
                if (read(read_pipe, buf.data(), meta.data_len) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                buf[meta.data_len] = '\0';
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(meta.window_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target->set_title(std::string(buf.data()));
                }
                send_response(header.request_id, {true});
            } else if (header.type == MsgType::SYSTEM_UI_SET_SIZE) {
                if (header.payload_size != sizeof(UISetSizePayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }
                UISetSizePayload payload{};
                if (read(read_pipe, &payload, sizeof(payload)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                if (payload.width <= 0 || payload.height <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(payload.window_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target->set_size(payload.width, payload.height);
                }
                send_response(header.request_id, {true});
            } else if (header.type == MsgType::SYSTEM_UI_SET_POSITION) {
                if (header.payload_size != sizeof(UISetPositionPayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }
                UISetPositionPayload payload{};
                if (read(read_pipe, &payload, sizeof(payload)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(payload.window_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target->set_position(payload.x, payload.y);
                }
                send_response(header.request_id, {true});
            } else if(header.type == MsgType::SYSTEM_UI_CENTER){
                if (header.payload_size != sizeof(UITargetPayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }
                UITargetPayload payload{};
                if (read(read_pipe, &payload, sizeof(payload)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(payload.window_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    target->center();
                }
                send_response(header.request_id, {true});
            } else if (header.type == MsgType::SYSTEM_UI_SET_VISIBILITY) {
                if (header.payload_size != sizeof(UISetVisibilityPayload)) {
                    send_response(header.request_id, {false});
                    continue;
                }
                UISetVisibilityPayload payload{};
                if (read(read_pipe, &payload, sizeof(payload)) <= 0) {
                    send_response(header.request_id, {false});
                    continue;
                }
                {
                    std::lock_guard<std::recursive_mutex> ui_guard(get_ui_lifetime_mutex());
                    WebViewWindow* target = get_macro_ui_window(payload.window_id);
                    if (!target) {
                        send_response(header.request_id, {false});
                        continue;
                    }
                    if (payload.visible) target->show();
                    else target->hide();
                }
                send_response(header.request_id, {true});
            }
        }
    }
}

void monitor_logs() {
    log_buffer.reserve(8192);
    char buffer[1024];
    while(running.load(std::memory_order_relaxed)){
        if (log_read_pipe == -1){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        while (log_read_pipe > 0) {
            ssize_t n = read(log_read_pipe, buffer, sizeof(buffer)-1);
            if (n <= 0) break;
            buffer[n] = '\0';
            // Bold the log so we know it's from Lua
            if(buffer[n-1] == '\n' || buffer[0] == '\n') lua_nextline = true;
            DEBUG_LOG((lua_nextline ? "\n\033[1;32m[LUA LOG]\033[0m: " : "") << buffer)
            lua_nextline = false;
            log_buffer.append(buffer, n);
            if(log_buffer.size() > 8192 || log_buffer.find('\n') != std::string::npos){
                emit_console_lines(log_buffer);
                log_buffer.clear();
            }
        }
    }
}

void monitor_resources() {
    // Max cooperative sleep the host may request. Keep moderate: scan paths apply a *fraction* per
    // hook (see macro_runner); very large values still made the PID overshoot before that fix.
    constexpr uint32_t kThrottleMaxSleepUs = 65000;
    constexpr uint32_t kThrottleUpStepUs = 350;
    constexpr uint32_t kThrottleDownStepUs = 500;
    constexpr double kCpuHysteresisPercent = 2.0;
    constexpr double kThrottleOverageSqScale = 35.0;
    constexpr uint32_t kThrottleBumperCapUs = 12000;
    while(running.load(std::memory_order_relaxed)){
        if (current_runner_pid == -1){
            cpu_throttle_sleep_us.store(0, std::memory_order_relaxed);
            exceeded = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        uint64_t last_total_cpu = 0;
        auto last = std::chrono::steady_clock::now();
        auto start = std::chrono::steady_clock::now();
        bool started = false;
        long long interval = 100000000;
        while (current_runner_pid > 0){
            if(started){
                interval = std::chrono::duration_cast<std::chrono::nanoseconds>((std::chrono::steady_clock::now() - last)).count();
            }
            auto stats = get_child_stats(current_runner_pid, last_total_cpu, interval);
            auto current = std::chrono::steady_clock::now();
            const uint64_t resident_limit = clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load());
            if(stats.ram_bytes > resident_limit){
                stop_macro();
                DEBUG_LOG("MACRO EXCEEDED RAM LIMIT: " << stats.ram_bytes/(1024*1024) << " > " << resident_limit/(1024*1024) << " MB" << std::endl);
            }

            const uint64_t vram_limit = clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load());
            if(stats.virtual_ram_bytes > vram_limit + initial_vram && current-start > std::chrono::seconds(3) && vram_limit_enabled.load()){
                DEBUG_LOG("initial: " << initial_vram / (1024*1024) << std::endl);
                stop_macro();
                DEBUG_LOG("MACRO EXCEEDED VIRTUAL RAM LIMIT: " << stats.virtual_ram_bytes/(1024*1024) << " > " << (vram_limit+initial_vram)/(1024*1024) << " MB" << std::endl);
            }

            const double cpu_throttle_target = clamp_cpu_target_percent(cpu_throttle_percent.load());
            const double cpu_kill_target = std::max(cpu_throttle_target, clamp_cpu_target_percent(cpu_kill_percent.load()));
            uint32_t throttle_sleep = cpu_throttle_sleep_us.load(std::memory_order_relaxed);

            if(stats.cpu_usage >= cpu_kill_target){
                if(!exceeded){
                    last_exceeded = current;
                    exceeded = true;
                }

                if(current - last_exceeded > std::chrono::seconds(3)){
                    stop_macro();
                    DEBUG_LOG("MACRO EXCEEDED CPU KILL LIMIT: " << stats.cpu_usage << " >= " << cpu_kill_target << "%" << std::endl);
                    continue;
                }
            }else{
                exceeded = false;
            }

            if (stats.cpu_usage > cpu_throttle_target + kCpuHysteresisPercent) {
                const double over = stats.cpu_usage - cpu_throttle_target;
                const uint32_t sq_bump =
                    static_cast<uint32_t>(std::min(static_cast<double>(kThrottleBumperCapUs), over * over * kThrottleOverageSqScale));
                const uint32_t next_sleep =
                    std::min(kThrottleMaxSleepUs, throttle_sleep + std::max(kThrottleUpStepUs, sq_bump));
                if (next_sleep != throttle_sleep) {
                    cpu_throttle_sleep_us.store(next_sleep, std::memory_order_relaxed);
                    send_runner_throttle_set(next_sleep);
                    DEBUG_LOG("CPU throttle increased to " << next_sleep << "us at usage " << stats.cpu_usage << "%" << std::endl);
                }
            } else if (stats.cpu_usage < cpu_throttle_target - kCpuHysteresisPercent) {
                uint32_t next_sleep = 0;
                if (throttle_sleep > kThrottleDownStepUs) {
                    next_sleep = throttle_sleep - kThrottleDownStepUs;
                }
                if (next_sleep != throttle_sleep) {
                    cpu_throttle_sleep_us.store(next_sleep, std::memory_order_relaxed);
                    if (next_sleep == 0) {
                        send_runner_throttle_clear();
                        DEBUG_LOG("CPU throttle cleared at usage " << stats.cpu_usage << "%" << std::endl);
                    } else {
                        send_runner_throttle_set(next_sleep);
                        DEBUG_LOG("CPU throttle reduced to " << next_sleep << "us at usage " << stats.cpu_usage << "%" << std::endl);
                    }
                }
            }

            last = current;
            started = true;
            const int poll_ms = (throttle_sleep > 0 || stats.cpu_usage > cpu_throttle_target) ? 33 : 100;
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
    }
}

inline uint32_t RGBA_to_ABGR(uint32_t rgba) {
    return ((rgba & 0xFF000000) >> 24) | // Move R to last byte
           ((rgba & 0x00FF0000) >> 8)  | // Move G to 3rd byte
           ((rgba & 0x0000FF00) << 8)  | // Move B to 2nd byte
           ((rgba & 0x000000FF) << 24);  // Move A to 1st byte
}

void render(){
    if(new_command){
        //DEBUG_LOG("[MAIN]: New draw commands detected, updating local copy." << std::endl);
        draw_command_mutex.lock();
        cached_commands = draw_commands;
        draw_command_mutex.unlock();
        new_command = false;
    }

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    for (const auto& item : cached_commands) {
        const DrawCommand& cmd = item.cmd;

        // ImGui expects colors in ABGR format (U32)
        // If your color is 0xRRGGBBAA, you might need to reshuffle bits
        ImU32 color = RGBA_to_ABGR(cmd.color); 

        switch (cmd.type) {
            case DrawCmdType::RECT: {
                //DEBUG_LOG("[MAIN]: Rendering RECT command at (" << cmd.x << ", " << cmd.y << ") size (" << cmd.w << "x" << cmd.h << ") color: " << std::hex << cmd.color << std::dec << std::endl);
                ImVec2 p_min = ImVec2(cmd.x, cmd.y);
                ImVec2 p_max = ImVec2(cmd.x + cmd.w, cmd.y + cmd.h);
                
                // AddRect(top_left, bottom_right, color, rounding, corners, thickness)
                if(cmd.fill){
                    ImU32 fill_color = RGBA_to_ABGR(cmd.fill_color);
                    draw_list->AddRectFilled(p_min, p_max, fill_color);
                }
                draw_list->AddRect(p_min, p_max, color, 0.0f, 0, cmd.thickness);
                break;
            }

            case DrawCmdType::TEXT: {
                // AddText(pos, color, text_begin)
                draw_list->AddText(ImVec2(cmd.x, cmd.y), color, cmd.text);
                break;
            }

            case DrawCmdType::LINE: {
                // For lines, x/y is start, w/h is end
                draw_list->AddLine(ImVec2(cmd.x, cmd.y), ImVec2(cmd.w, cmd.h), color, cmd.thickness);
                break;
            }

            case DrawCmdType::ARC: {
                const ImVec2 c(cmd.x, cmd.y);
                const float r = cmd.w;
                float a0 = cmd.angle_start;
                float a1 = cmd.angle_end;
                if (cmd.flags & kDrawCmdFlagCounterclockwise) {
                    std::swap(a0, a1);
                }
                if (cmd.fill) {
                    const ImU32 fc = RGBA_to_ABGR(cmd.fill_color);
                    draw_list->PathClear();
                    draw_list->PathLineTo(c);
                    draw_list->PathArcTo(c, r, a0, a1);
                    draw_list->PathFillConvex(fc);
                }
                draw_list->PathClear();
                draw_list->PathArcTo(c, r, a0, a1);
                draw_list->PathStroke(color, ImDrawFlags_None, cmd.thickness);
                break;
            }

            case DrawCmdType::CIRCLE: {
                const ImVec2 c(cmd.x, cmd.y);
                const float rad = cmd.w;
                if (cmd.fill) {
                    draw_list->AddCircleFilled(c, rad, RGBA_to_ABGR(cmd.fill_color));
                }
                draw_list->AddCircle(c, rad, color, 0, cmd.thickness);
                break;
            }

            case DrawCmdType::ELLIPSE: {
                const ImVec2 c(cmd.x, cmd.y);
                const ImVec2 radii(cmd.w, cmd.h);
                float a0 = cmd.angle_start;
                float a1 = cmd.angle_end;
                if (cmd.flags & kDrawCmdFlagCounterclockwise) {
                    std::swap(a0, a1);
                }
                const float span = std::fabs(a1 - a0);
                const bool full_ellipse = (span >= float(M_PI) * 2.0f - 1e-2f)
                    || (cmd.angle_start == 0.f && cmd.angle_end >= 6.28f);
                if (full_ellipse) {
                    if (cmd.fill) {
                        draw_list->AddEllipseFilled(c, radii, RGBA_to_ABGR(cmd.fill_color), cmd.rotation);
                    }
                    draw_list->AddEllipse(c, radii, color, cmd.rotation, 0, cmd.thickness);
                } else {
                    if (cmd.fill) {
                        draw_list->PathClear();
                        draw_list->PathLineTo(c);
                        draw_list->PathEllipticalArcTo(c, radii, cmd.rotation, a0, a1);
                        draw_list->PathFillConvex(RGBA_to_ABGR(cmd.fill_color));
                    }
                    draw_list->PathClear();
                    draw_list->PathEllipticalArcTo(c, radii, cmd.rotation, a0, a1);
                    draw_list->PathStroke(color, ImDrawFlags_None, cmd.thickness);
                }
                break;
            }

            case DrawCmdType::POLYGON: {
                if (item.points.size() < 3) {
                    break;
                }
                if (cmd.fill) {
                    draw_list->AddConvexPolyFilled(
                        item.points.data(),
                        static_cast<int>(item.points.size()),
                        RGBA_to_ABGR(cmd.fill_color));
                }
                draw_list->AddPolyline(
                    item.points.data(),
                    static_cast<int>(item.points.size()),
                    color,
                    ImDrawFlags_Closed,
                    cmd.thickness);
                break;
            }

            case DrawCmdType::POLYLINE: {
                if (item.points.size() < 2) {
                    break;
                }
                draw_list->AddPolyline(
                    item.points.data(),
                    static_cast<int>(item.points.size()),
                    color,
                    0,
                    cmd.thickness);
                break;
            }

            case DrawCmdType::QUADRATIC_CURVE: {
                if (item.points.size() != 3) {
                    break;
                }
                const ImVec2* p = item.points.data();
                draw_list->PathClear();
                draw_list->PathLineTo(p[0]);
                draw_list->PathBezierQuadraticCurveTo(p[1], p[2]);
                draw_list->PathStroke(color, 0, cmd.thickness);
                break;
            }

            case DrawCmdType::BEZIER_CURVE: {
                if (item.points.size() != 4) {
                    break;
                }
                const ImVec2* p = item.points.data();
                draw_list->PathClear();
                draw_list->PathLineTo(p[0]);
                draw_list->PathBezierCubicCurveTo(p[1], p[2], p[3]);
                draw_list->PathStroke(color, 0, cmd.thickness);
                break;
            }

            default:
                break;
        }
    }

}

int main(int argc, char* argv[]) {
    for(auto& perm : permissions){
        perm.store(0);
    }
    disable_app_nap();
    get_screen_dim(screen_w, screen_h);

    auto server_port = start_file_server(get_app_bundle_path()+"/Contents/Resources");
    if(server_port == -1){
        std::cerr << "Fatal: Could not start internal file server." << std::endl;
        return 1;
    }
    ui_server_port = server_port;
    std::cout << "using port " << server_port << std::endl;

    overlay = app.create_window("Machotkey Overlay", 0, 0, screen_w, screen_h, false, true);
    overlay->load_url("http://127.0.0.1:"+std::to_string(server_port)+"/overlay/macro_controls.html");
    overlay->set_ignores_mouse(true);
    overlay->send_to_js("window.soft_hide_taskbar();");
    read_global_rate_settings();
    read_global_cpu_limit_settings();
    read_global_memory_limit_settings();
    headless_mode_enabled.store(app.is_headless_mode_enabled());
    app.set_menu_action_handler([](AppMenuAction action) {
        switch (action) {
            case AppMenuAction::ToggleHeadless:
                apply_headless_mode(app.is_headless_mode_enabled());
                break;
            case AppMenuAction::LoadProject: {
                const std::string result = load_project_from_dialog();
                if (result.rfind("success;", 0) == 0) {
                    emit_status_line("Project loaded", "success");
                } else if (!result.empty()) {
                    emit_status_line(result, "error");
                }
                refresh_menu_runtime_state();
                break;
            }
            case AppMenuAction::RunProjectMacro: {
                const std::string result = run_project_macro_from_state();
                if (result.rfind("error:", 0) == 0) emit_status_line(result, "error");
                refresh_menu_runtime_state();
                break;
            }
            case AppMenuAction::RunQuickScript: {
                std::string quick_filename;
                std::string quick_code;
                if (!prompt_quick_script_input(quick_filename, quick_code)) break;
                nlohmann::json payload = nlohmann::json::object({
                    {"filename", quick_filename},
                    {"code", quick_code}
                });
                const std::string result = run_quick_script_payload(payload.dump());
                if (result.rfind("error:", 0) == 0) emit_status_line(result, "error");
                refresh_menu_runtime_state();
                break;
            }
            case AppMenuAction::StopMacro:
                stop_macro_from_ui();
                refresh_menu_runtime_state();
                break;
            case AppMenuAction::PauseResumeMacro:
                toggle_pause_resume_from_ui();
                break;
        }
    });
    refresh_menu_runtime_state();

    app.bind("get_max_cpu", [](const std::string& data)->std::string{
        (void)data;
        return std::to_string(max_cpu_target_percent());
    });

    app.bind("run_script", [](const std::string& data)->std::string{
        (void)data;
        if(current_runner_pid != -1){
            DEBUG_LOG("[MAIN]: New macro run. Killing old Runner (PID: " << current_runner_pid << ")" << std::endl);
            stop_macro();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if(current_project_dir.empty() || current_manifest.entry.empty()){
            return "error: No project currently loaded";
        }
        if(current_manifest.raw.empty()){
            return "error: Manifest not loaded";
        }

        for(auto& perm : permissions){
            perm.store(0);
        }
        current_permission_grants.clear();
        macro_filename = current_manifest.entry;

        const std::filesystem::path project_dir(current_project_dir);
        const auto entry_path = resolve_manifest_path(project_dir, current_manifest.entry);
        std::string entry_code = load_file_or_empty(entry_path);
        if (entry_code.empty()) {
            return "error: Could not read manifest entry: " + entry_path.string();
        }

        std::unordered_map<std::string, bool> grants;
        const auto code_hash = compute_manifest_code_hash(project_dir, current_manifest);
        auto record = read_project_permission_record(project_dir);
        record.grants = filter_grants_to_manifest(record.grants, current_manifest);
        bool used_persisted = false;
        std::string reset_reason;
        if (!record.grants.empty()) {
            if (!record.reset_on_code_change) {
                grants = record.grants;
                used_persisted = true;
            } else if (code_hash.has_value() && !record.code_hash.empty() && record.code_hash == code_hash.value()) {
                grants = record.grants;
                used_persisted = true;
            } else if (code_hash.has_value() && !record.code_hash.empty() && record.code_hash != code_hash.value()) {
                reset_reason = "permissions reset: project code changed since last approval";
            } else if (!code_hash.has_value()) {
                reset_reason = "permissions reset: unable to hash manifest files (missing/unreadable file)";
            }
        }

        if (!used_persisted) {
            if (!reset_reason.empty()) {
                emit_status_line(reset_reason, "warning");
            }
            // After code change or hash failure, old grants must not short-circuit prompts: every manifest
            // permission needs a fresh decision. Otherwise prompt_for_missing_permissions skips keys still in record.grants.
            std::optional<std::unordered_map<std::string, bool>> prompt_result;
            if (!reset_reason.empty()) {
                prompt_result = prompt_for_permissions(macro_filename, current_manifest);
            } else {
                prompt_result = prompt_for_missing_permissions(macro_filename, current_manifest, record.grants);
            }
            if (!prompt_result.has_value()) {
                return "error: Required permission denied";
            }
            grants = prompt_result.value();
            record.grants = filter_grants_to_manifest(grants, current_manifest);
            record.code_hash = code_hash.value_or("");
            write_project_permission_record(project_dir, record);
        } else if (record.grants.size() < collect_manifest_permissions(current_manifest).size()) {
            // Persisted data exists but is incomplete for current manifest; prompt only missing.
            auto prompt_result = prompt_for_missing_permissions(macro_filename, current_manifest, grants);
            if (!prompt_result.has_value()) {
                return "error: Required permission denied";
            }
            grants = prompt_result.value();
            record.grants = filter_grants_to_manifest(grants, current_manifest);
            record.code_hash = code_hash.value_or("");
            write_project_permission_record(project_dir, record);
        }

        if (has_denied_required_permission(grants, current_manifest)) {
            return "error: Required permission denied";
        }

        std::string system_permission_error;
        if (!ensure_app_level_system_permissions(macro_filename, grants, system_permission_error)) {
            return "error: " + system_permission_error;
        }

        merge_record_resource_limits_with_global_settings(record);
        const auto approved_before_negotiation = record.approved_rates;
        const auto approved_resources_before_negotiation = record.approved_resource_limits;
        const auto effective_rates = negotiate_effective_rates(macro_filename, current_manifest.requested_rates, record);
        const auto effective_resource_limits = negotiate_effective_resource_limits(macro_filename, current_manifest.requested_rates, record);
        const bool rates_changed = !rate_limits_equal(approved_before_negotiation, record.approved_rates);
        const bool resources_changed = !resource_limits_equal(approved_resources_before_negotiation, record.approved_resource_limits);
        if (rates_changed || resources_changed) {
            write_project_permission_record(project_dir, record);
        }
        set_current_runtime_rates(effective_rates);
        set_runtime_resource_limits(effective_resource_limits);

        apply_permission_grants(grants);

        const std::string sandbox_profile = build_dynamic_sandbox_profile(SB_PROFILE, project_dir, current_manifest);
        const std::string grants_json = nlohmann::json(grants).dump();

        auto filename = entry_path.filename().string();
        if(filename.empty()) filename = current_manifest.entry;
        if(filename.find("`") != std::string::npos){
            return "error: Invalid character '`' in entry filename";
        }

        DEBUG_LOG("[MAIN]: Starting manifest macro: " << filename << std::endl);
        macro_filename = filename;
        project_manifest_mode = true;

        start_project_macro_async(
            filename,
            entry_code,
            current_project_dir,
            sandbox_profile,
            current_manifest.raw,
            grants_json,
            current_runner_pid,
            write_pipe,
            read_pipe,
            log_read_pipe
        );

        overlay->get_position_async([filename](int x, int y){
            if (!overlay) return;
            overlay->x = x;
            overlay->y = y;

            overlay->set_opacity(0.0);
            overlay->send_to_js("window.soft_hide_taskbar();");
            overlay->set_ignores_mouse(true);
            overlay->set_position(0, 0);
            overlay->set_size(screen_w, screen_h);
            overlay->send_to_js("window.set_name(" + escape_string(filename) + "); window.request_dimensions(); window.set_btn_states(true, false, false);");
        });
        
        uint64_t unused;
        initial_vram = get_child_stats(current_runner_pid, unused, unused).virtual_ram_bytes;
        cpu_throttle_sleep_us.store(0, std::memory_order_relaxed);
        exceeded = false;
        paused = false;
        refresh_menu_runtime_state();
        return "success";
    });

    app.bind("run_quick_script", [](const std::string& data)->std::string{
        if(current_runner_pid != -1){
            DEBUG_LOG("[MAIN]: New quick script run. Killing old Runner (PID: " << current_runner_pid << ")" << std::endl);
            stop_macro();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::string parse_error;
        auto payload_opt = parse_quick_script_payload(data, parse_error);
        if (!payload_opt.has_value()) {
            return "error: " + parse_error;
        }
        auto payload = payload_opt.value();
        macro_filename = payload.filename;

        if(macro_filename.find("`") != std::string::npos){
            return "error: Invalid character '`' in filename";
        }

        for(auto& perm : permissions){
            perm.store(0);
        }
        current_permission_grants.clear();

        std::filesystem::path quick_project_dir;
        if (!payload.project_dir.empty()) {
            quick_project_dir = std::filesystem::path(payload.project_dir);
        } else if (!current_project_dir.empty()) {
            quick_project_dir = std::filesystem::path(current_project_dir);
        } else {
            quick_project_dir = std::filesystem::temp_directory_path() / "machotkey_quick";
        }
        quick_project_dir = quick_project_dir.lexically_normal();

        ProjectManifest quick_manifest;
        quick_manifest.entry = payload.filename;
        quick_manifest.permissions = payload.permissions;
        quick_manifest.files = payload.files;
        quick_manifest.requested_rates = payload.requested_rates;
        {
            std::unordered_map<std::string, ManifestFileRule> dedup_files;
            for (const auto& file : quick_manifest.files) {
                if (file.path.empty()) continue;
                auto it = dedup_files.find(file.path);
                if (it == dedup_files.end()) {
                    dedup_files.emplace(file.path, file);
                } else {
                    it->second.read = it->second.read || file.read;
                    it->second.write = it->second.write || file.write;
                    it->second.optional = it->second.optional && file.optional;
                }
            }
            ManifestFileRule entry_rule;
            entry_rule.path = quick_manifest.entry;
            entry_rule.read = true;
            entry_rule.write = false;
            entry_rule.optional = false;
            auto entry_it = dedup_files.find(entry_rule.path);
            if (entry_it == dedup_files.end()) {
                dedup_files.emplace(entry_rule.path, entry_rule);
            } else {
                entry_it->second.read = true;
                entry_it->second.optional = false;
            }
            quick_manifest.files.clear();
            for (const auto& [_, file] : dedup_files) {
                quick_manifest.files.push_back(file);
            }
            std::sort(quick_manifest.files.begin(), quick_manifest.files.end(), [](const ManifestFileRule& a, const ManifestFileRule& b) {
                return a.path < b.path;
            });
        }

        nlohmann::json raw_manifest_json = nlohmann::json::object({
            {"name", "Quick Script"},
            {"version", "1.0"},
            {"entry", quick_manifest.entry},
            {"files", nlohmann::json::array()},
            {"permissions", nlohmann::json::array()},
            {"rate_limits", manifest_requested_rates_to_json(quick_manifest.requested_rates)}
        });
        for (const auto& file : quick_manifest.files) {
            raw_manifest_json["files"].push_back(nlohmann::json::object({
                {"path", file.path},
                {"read", file.read},
                {"write", file.write},
                {"optional", file.optional}
            }));
        }
        for (const auto& permission : quick_manifest.permissions) {
            raw_manifest_json["permissions"].push_back(nlohmann::json::object({
                {"name", permission.name},
                {"optional", permission.optional}
            }));
        }
        quick_manifest.raw = raw_manifest_json.dump();

        auto prompt_result = prompt_for_permissions(macro_filename, quick_manifest);
        if (!prompt_result.has_value()) {
            return "error: Required permission denied";
        }
        auto grants = prompt_result.value();
        std::string quick_system_permission_error;
        if (!ensure_app_level_system_permissions(macro_filename, grants, quick_system_permission_error)) {
            return "error: " + quick_system_permission_error;
        }
        auto quick_record = read_project_permission_record(quick_project_dir);
        merge_record_resource_limits_with_global_settings(quick_record);
        const auto quick_approved_rates_before = quick_record.approved_rates;
        const auto quick_approved_resources_before = quick_record.approved_resource_limits;
        const auto quick_effective_rates = negotiate_effective_rates(macro_filename, quick_manifest.requested_rates, quick_record);
        const auto quick_effective_resource_limits =
            negotiate_effective_resource_limits(macro_filename, quick_manifest.requested_rates, quick_record);
        const bool quick_rates_changed = !rate_limits_equal(quick_approved_rates_before, quick_record.approved_rates);
        const bool quick_resources_changed =
            !resource_limits_equal(quick_approved_resources_before, quick_record.approved_resource_limits);
        if (quick_rates_changed || quick_resources_changed) {
            write_project_permission_record(quick_project_dir, quick_record);
        }
        set_current_runtime_rates(quick_effective_rates);
        set_runtime_resource_limits(quick_effective_resource_limits);
        apply_permission_grants(grants);

        const std::string sandbox_profile = build_dynamic_sandbox_profile(SB_PROFILE, quick_project_dir, quick_manifest);
        const std::string grants_json = nlohmann::json(grants).dump();

        DEBUG_LOG("[MAIN]: Starting quick script: " << macro_filename << std::endl);
        project_manifest_mode = false;

        start_project_macro_async(
            macro_filename,
            payload.code,
            quick_project_dir.string(),
            sandbox_profile,
            quick_manifest.raw,
            grants_json,
            current_runner_pid,
            write_pipe,
            read_pipe,
            log_read_pipe
        );

        overlay->get_position_async([](int x, int y){
            if (!overlay) return;
            overlay->x = x;
            overlay->y = y;

            overlay->set_opacity(0.0);
            overlay->send_to_js("window.soft_hide_taskbar();");
            overlay->set_ignores_mouse(true);
            overlay->set_position(0, 0);
            overlay->set_size(screen_w, screen_h);
            overlay->send_to_js("window.set_name('Quick Script'); window.request_dimensions(); window.set_btn_states(true, false, false);");
        });

        uint64_t unused;
        initial_vram = get_child_stats(current_runner_pid, unused, unused).virtual_ram_bytes;
        cpu_throttle_sleep_us.store(0, std::memory_order_relaxed);
        exceeded = false;
        paused = false;
        refresh_menu_runtime_state();
        return "success";
    });

    app.bind("stop_script", [](const std::string& data){
        (void)data;
        stop_macro();
        return "success";
    });

    app.bind("overlay_stop_macro", [](const std::string& data){
        stop_macro();
        return "success";
    });

    app.bind("overlay_pause_macro", [](const std::string& data){
        pause_macro();
        overlay->send_to_js("window.set_btn_states(false, true, false);");
        return "success";
    });

    app.bind("overlay_start_macro", [](const std::string& data){
        resume_macro();
        overlay->send_to_js("window.set_btn_states(true, false, false);");
        return "success";
    });

    app.bind("overlay_dimensions", [](const std::string& data)->std::string{
        DEBUG_LOG("[MAIN]: Setting overlay dimensions to " << data << std::endl);
        std::stringstream ss(data);
        float w, h;
        ss >> w >> h;

        w+=20;
        h+=20;

        overlay->set_size(w, h);
        if(overlay->x == -1 || overlay->y == -1){
            overlay->set_position(screen_w/2 - w/2, 50);
        }else{
            overlay->set_position(overlay->x, overlay->y);
        }
        overlay->set_ignores_mouse(false);
        overlay->send_to_js("window.show_taskbar();");
        overlay->set_opacity(1.0);

        return "success";
    });

    app.bind("lsp_message", [](const std::string& data){
        lua_ls_manager.send_message(data);
        return "success";
    });

    app.bind("console.log", [](const std::string& data){
        DEBUG_LOG("[CONSOLE LOG]: " << data << std::endl);
        return "success";
    });

    app.bind("console.error", [](const std::string& data){
        DEBUG_LOG("[CONSOLE ERROR]: " << data << std::endl);
        return "success";
    });

    app.bind("load_project", [](const std::string& data) -> std::string {
        (void)data;
        std::string path = open_project_dialog();
        
        if(path.empty()) return "error;No folder selected";

        const std::filesystem::path manifest_path = std::filesystem::path(path) / "manifest.json";
        std::string raw_manifest = load_file_or_empty(manifest_path);
        if(raw_manifest.empty()) return "error;'manifest.json' not found!";

        ProjectManifest parsed_manifest;
        std::string error;
        if(!parse_project_manifest(raw_manifest, parsed_manifest, error)) {
            return "error;" + error;
        }

        current_project_dir = path;
        current_manifest = std::move(parsed_manifest);
        project_manifest_mode = true;
        refresh_menu_runtime_state();

        return std::string("success;") + path;
    });

    app.bind("get_project_permission_settings", [](const std::string& data)->std::string{
        (void)data;
        if (current_project_dir.empty()) {
            return nlohmann::json::object({
                {"project_dir", ""},
                {"reset_on_code_change", true},
                {"permissions", nlohmann::json::array()},
                {"grants", nlohmann::json::object()},
                {"approved_rates", rate_limits_to_json(clamp_rate_limits(RateLimitConfig{}))},
                {"requested_rates", nlohmann::json::object()},
                {"effective_rates", rate_limits_to_json(clamp_rate_limits(RateLimitConfig{}))}
            }).dump();
        }
        auto record = read_project_permission_record(std::filesystem::path(current_project_dir));
        record.grants = filter_grants_to_manifest(record.grants, current_manifest);
        record.approved_rates = apply_global_rate_ceiling(record.approved_rates);
        nlohmann::json grants = nlohmann::json::object();
        for (const auto& [name, value] : record.grants) grants[name] = value;
        RateLimitConfig effective = record.approved_rates;
        if (current_manifest.requested_rates.mouse_events_per_sec.has_value()) {
            effective.mouse_events_per_sec = std::min(effective.mouse_events_per_sec, current_manifest.requested_rates.mouse_events_per_sec.value());
        }
        if (current_manifest.requested_rates.keyboard_events_per_sec.has_value()) {
            effective.keyboard_events_per_sec = std::min(effective.keyboard_events_per_sec, current_manifest.requested_rates.keyboard_events_per_sec.value());
        }
        if (current_manifest.requested_rates.keyboard_text_chars_per_sec.has_value()) {
            effective.keyboard_text_chars_per_sec = std::min(effective.keyboard_text_chars_per_sec, current_manifest.requested_rates.keyboard_text_chars_per_sec.value());
        }
        nlohmann::json resp = nlohmann::json::object({
            {"project_dir", current_project_dir},
            {"reset_on_code_change", record.reset_on_code_change},
            {"permissions", manifest_permissions_to_json(current_manifest)},
            {"grants", grants},
            {"approved_rates", rate_limits_to_json(record.approved_rates)},
            {"requested_rates", manifest_requested_rates_to_json(current_manifest.requested_rates)},
            {"effective_rates", rate_limits_to_json(apply_global_rate_ceiling(effective))}
        });
        return resp.dump();
    });

    app.bind("set_project_permission_settings", [](const std::string& data)->std::string{
        if (current_project_dir.empty()) {
            return "error: No project currently loaded";
        }
        try {
            auto json = nlohmann::json::parse(data);
            auto record = read_project_permission_record(std::filesystem::path(current_project_dir));
            record.grants = filter_grants_to_manifest(record.grants, current_manifest);
            if (json.contains("reset_on_code_change") && json["reset_on_code_change"].is_boolean()) {
                record.reset_on_code_change = json["reset_on_code_change"].get<bool>();
            }
            if (json.contains("grants") && json["grants"].is_object()) {
                for (const auto& permission : collect_manifest_permissions(current_manifest)) {
                    if (json["grants"].contains(permission.name) && json["grants"][permission.name].is_boolean()) {
                        record.grants[permission.name] = json["grants"][permission.name].get<bool>();
                    }
                }
            }
            if (json.contains("approved_rates")) {
                parse_rate_limits_from_json(json["approved_rates"], record.approved_rates);
                record.approved_rates = apply_global_rate_ceiling(record.approved_rates);
            }
            write_project_permission_record(std::filesystem::path(current_project_dir), record);
            return "success";
        } catch (const std::exception& e) {
            return std::string("error: ") + e.what();
        }
    });

    app.bind("reset_project_permissions", [](const std::string& data)->std::string{
        (void)data;
        if (current_project_dir.empty()) {
            return "error: No project currently loaded";
        }
        auto record = read_project_permission_record(std::filesystem::path(current_project_dir));
        record.grants.clear();
        record.code_hash.clear();
        write_project_permission_record(std::filesystem::path(current_project_dir), record);
        return "success";
    });
    app.bind("get_global_rate_settings", [](const std::string& data)->std::string{
        (void)data;
        const auto max_rates = read_global_rate_settings();
        return nlohmann::json::object({
            {"max_rates", rate_limits_to_json(max_rates)}
        }).dump();
    });
    app.bind("set_global_rate_settings", [](const std::string& data)->std::string{
        try {
            auto json = nlohmann::json::parse(data);
            auto current = read_global_rate_settings();
            if (json.contains("max_rates")) {
                parse_rate_limits_from_json(json["max_rates"], current);
            }
            write_global_rate_settings(current);
            // If a macro is running, enforce the newly lowered ceiling immediately.
            set_current_runtime_rates(RateLimitConfig{
                current_mouse_rate_per_sec.load(),
                current_keyboard_rate_per_sec.load(),
                current_keyboard_text_chars_per_sec.load()
            });
            return "success";
        } catch (const std::exception& e) {
            return std::string("error: ") + e.what();
        }
    });
    app.bind("get_resource_limits", [](const std::string& data)->std::string{
        (void)data;
        const uint64_t resident_limit = clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load());
        const uint64_t virtual_limit = clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load());
        const double cpu_throttle_target = clamp_cpu_target_percent(cpu_throttle_percent.load());
        const double cpu_kill_target = std::max(cpu_throttle_target, clamp_cpu_target_percent(cpu_kill_percent.load()));
        return nlohmann::json::object({
            {"cpu_throttle_percent", cpu_throttle_target},
            {"cpu_kill_percent", cpu_kill_target},
            {"cpu_throttle_sleep_us", cpu_throttle_sleep_us.load()},
            {"resident_ram_limit_bytes", resident_limit},
            {"resident_ram_limit_mb", static_cast<double>(resident_limit) / (1024.0 * 1024.0)},
            {"virtual_ram_limit_bytes", virtual_limit},
            {"virtual_ram_limit_mb", static_cast<double>(virtual_limit) / (1024.0 * 1024.0)},
            {"virtual_ram_limit_enabled", vram_limit_enabled.load()}
        }).dump();
    });
    app.bind("set_resource_limits", [](const std::string& data)->std::string{
        try {
            auto json = nlohmann::json::parse(data);
            if (!json.is_object()) return "error: payload must be an object";
            bool updated_any = false;
            double throttle_target = clamp_cpu_target_percent(cpu_throttle_percent.load());
            double kill_target = std::max(throttle_target, clamp_cpu_target_percent(cpu_kill_percent.load()));
            
            if (json.contains("cpu_throttle_percent")) {
                if (!json["cpu_throttle_percent"].is_number()) return "error: cpu_throttle_percent must be numeric";
                throttle_target = clamp_cpu_target_percent(json["cpu_throttle_percent"].get<double>());
                updated_any = true;
            } else if (json.contains("cpu_target_percent")) {
                if (!json["cpu_target_percent"].is_number()) return "error: cpu_target_percent must be numeric";
                throttle_target = clamp_cpu_target_percent(json["cpu_target_percent"].get<double>());
                updated_any = true;
            }
            if (json.contains("cpu_kill_percent")) {
                if (!json["cpu_kill_percent"].is_number()) return "error: cpu_kill_percent must be numeric";
                kill_target = clamp_cpu_target_percent(json["cpu_kill_percent"].get<double>());
                updated_any = true;
            }

            DEBUG_LOG("\n\n[MAIN]: Setting global CPU limits to " << throttle_target << " " << kill_target << "\n\n\n");

            if (kill_target < throttle_target) kill_target = throttle_target;
            write_global_cpu_limit_settings(throttle_target, kill_target);
            uint64_t resident_limit = resident_ram_limit_bytes.load();
            uint64_t virtual_limit = virtual_ram_limit_bytes.load();
            bool virtual_enabled = vram_limit_enabled.load();
            if (json.contains("resident_ram_limit_mb")) {
                if (!json["resident_ram_limit_mb"].is_number()) return "error: resident_ram_limit_mb must be numeric";
                const double mb = json["resident_ram_limit_mb"].get<double>();
                if (!std::isfinite(mb) || mb <= 0.0) return "error: resident_ram_limit_mb must be > 0";
                resident_limit = static_cast<uint64_t>(mb * 1024.0 * 1024.0);
                updated_any = true;
            }
            if (json.contains("virtual_ram_limit_mb")) {
                if (!json["virtual_ram_limit_mb"].is_number()) return "error: virtual_ram_limit_mb must be numeric";
                const double mb = json["virtual_ram_limit_mb"].get<double>();
                if (!std::isfinite(mb) || mb <= 0.0) return "error: virtual_ram_limit_mb must be > 0";
                virtual_limit = static_cast<uint64_t>(mb * 1024.0 * 1024.0);
                updated_any = true;
            }
            if (json.contains("virtual_ram_limit_enabled")) {
                if (!json["virtual_ram_limit_enabled"].is_boolean()) return "error: virtual_ram_limit_enabled must be boolean";
                virtual_enabled = json["virtual_ram_limit_enabled"].get<bool>();
                updated_any = true;
            }
            if (!updated_any) {
                return "error: no resource fields provided";
            }
            write_global_memory_limit_settings(resident_limit, virtual_limit, virtual_enabled);
            // Keep per-project approved_resource_limits in sync with the global panel. Otherwise the next
            // run_script negotiates from stale JSON while get_resource_limits shows the updated atomics.
            if (!current_project_dir.empty()) {
                auto record = read_project_permission_record(std::filesystem::path(current_project_dir));
                const ResourceLimitConfig new_approved = clamp_resource_limits(ResourceLimitConfig{
                    cpu_throttle_percent.load(),
                    cpu_kill_percent.load(),
                    resident_ram_limit_bytes.load(),
                    virtual_ram_limit_bytes.load(),
                    vram_limit_enabled.load()});
                if (!resource_limits_equal(record.approved_resource_limits, new_approved)) {
                    record.approved_resource_limits = new_approved;
                    write_project_permission_record(std::filesystem::path(current_project_dir), record);
                }
            }
            const double cpu_throttle_clamped = clamp_cpu_target_percent(cpu_throttle_percent.load());
            const double cpu_kill_clamped = std::max(cpu_throttle_clamped, clamp_cpu_target_percent(cpu_kill_percent.load()));
            const uint64_t resident_clamped = clamp_resident_ram_limit_bytes(resident_ram_limit_bytes.load());
            const uint64_t virtual_clamped = clamp_virtual_ram_limit_bytes(virtual_ram_limit_bytes.load());
            return nlohmann::json::object({
                {"cpu_throttle_percent", cpu_throttle_clamped},
                {"cpu_kill_percent", cpu_kill_clamped},
                {"resident_ram_limit_mb", static_cast<double>(resident_clamped) / (1024.0 * 1024.0)},
                {"virtual_ram_limit_mb", static_cast<double>(virtual_clamped) / (1024.0 * 1024.0)},
                {"virtual_ram_limit_enabled", vram_limit_enabled.load()}
            }).dump();
        } catch (const std::exception& e) {
            return std::string("error: ") + e.what();
        }
    });
    //app.run_blocking();
    /*capturer.start(60);
    
    uint8_t* dataOut;
    size_t out_width, out_height, out_bytes_per_row, n_times = 0;
    auto start = std::chrono::steady_clock::now();

    capturer.get_next_frame(&dataOut, out_width, out_height, out_bytes_per_row);
    std::cout << "first 8 bytes of frame: ";
    for(int i = 0; i < 8; i++){
        std::cout << (int)dataOut[i] << " ";
    }
    std::cout << std::endl;
    while(true){
        find_exact_color(dataOut, out_width, out_height, out_bytes_per_row, {0,0,out_width,out_height}, {0,0,255});
        n_times += 1;
        if(n_times % 1000 == 0){
            auto end =  std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "Performed " << n_times << " searches in " << diff << " ms (" << (n_times / (diff / 1000.0)) << " searches/sec)" << std::endl;
            start = end;
            n_times = 0;
        }
    }*/

    /*if(argc <= 1){
        std::cerr << "Usage: " << argv[0] << " <filename.lua>" << std::endl;
        return 1;
    }*/

    // For testing: manually add a keybind to watch (e.g., F1)
    // In a real app, you'd populate this when the Runner tells you to
    // active_keybinds[{122, 0}] = "F1_Macro"; 

    populate_key_map();

    // Thread to handle logs from the Runner
    std::thread log_thread(monitor_logs);
    log_thread.detach();

    // Thread to handle data from the Runner
    std::thread data_thread(monitor_data);
    data_thread.detach();

    // Thread to monitor resouce usage of the Runner
    std::thread resource_monitor(monitor_resources);
    resource_monitor.detach();
    
    std::thread listener_thread(start_listener);
    listener_thread.detach();

    start_screencapture_watchdog(capturer, main_shm, current_region, current_fps);

    InitOverlay();
    SetRenderCallback(render);
    app.set_accessory(false);

    /*lua_ls_manager.set_message_callback([](const std::string& message) {
        main_win->send_to_js("if(window.receiveLSPMessage) window.receiveLSPMessage(" + escape_string(message) + ");");
    });*/

    apply_headless_mode(headless_mode_enabled.load());
    /*std::thread lsp_enabler([](){
        while(!main_win->is_ready()){
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        DEBUG_LOG("starting lua language server..." << std::endl);
        lua_ls_manager.start(get_app_bundle_path()+"/Contents/Resources/lua-language-server/bin/lua-language-server");
    });
    lsp_enabler.detach();*/
    app.set_terminate_handler([] { prepare_app_terminate(); });
    app.run_blocking();

    running = false;

    return 0;
}