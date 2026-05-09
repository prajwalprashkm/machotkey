#if defined(__APPLE__ )
#include "utils.h"
#include "objc_utils.h"

#include <unistd.h>
#include <signal.h>
#include <Carbon/Carbon.h>

std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(str); // Create a stringstream from the input string

    // Use getline to extract tokens separated by the delimiter
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

std::unordered_map<std::string, uint16_t> key_map;
std::vector<std::string> shift_required;

void populate_key_map() {
    // 1. Add static "Special" keys that don't change based on layout
    key_map["esc"] = kVK_Escape;
    key_map["enter"] = kVK_Return;
    key_map["space"] = kVK_Space;
    key_map[" "] = kVK_Space;
    key_map["tab"] = kVK_Tab;
    key_map["up"] = kVK_UpArrow;
    key_map["down"] = kVK_DownArrow;
    key_map["left"] = kVK_LeftArrow;
    key_map["right"] = kVK_RightArrow;
    key_map["backspace"] = kVK_Delete;
    key_map["delete"] = kVK_Delete;

    key_map["shift"] = kVK_Shift;
    key_map["rshift"] = kVK_RightShift;
    key_map["ctrl"] = kVK_Control;
    key_map["cmd"] = kVK_Command;
    key_map["rcmd"] = kVK_RightCommand;
    key_map["alt"] = kVK_Option;
    key_map["option"] = kVK_Option;
    key_map["ralt"] = kVK_RightOption;
    key_map["roption"] = kVK_RightOption;

    // 2. Programmatically map A-Z and 0-9 based on current layout
    const char* chars_to_map = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
    
    for (int i = 0; chars_to_map[i] != '\0'; ++i) {
        char c = chars_to_map[i];
        std::string s(1, c);
        
        int32_t code = get_keycode_for_char(&c);
        if (code != -1) {
            key_map[s] = (uint16_t)code;
        }
    }
}

int32_t get_keycode_for_char(const char* character) {
    TISInputSourceRef currentKeyboard = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    CFDataRef layoutData = (CFDataRef)TISGetInputSourceProperty(currentKeyboard, kTISPropertyUnicodeKeyLayoutData);
    const UCKeyboardLayout *keyboardLayout = (const UCKeyboardLayout *)CFDataGetBytePtr(layoutData);

    UInt32 deadKeyState = 0;
    UniCharCount maxStringLength = 255;
    UniCharCount actualStringLength;
    UniChar unicodeString[maxStringLength];

    // Iterate through all possible keycodes (0-127)
    for (int i = 0; i < 128; i++) {
        UCKeyTranslate(keyboardLayout, i, kUCKeyActionDisplay, 0, LMGetKbdType(),
                       kUCKeyTranslateNoDeadKeysBit, &deadKeyState,
                       maxStringLength, &actualStringLength, unicodeString);
        
        if (unicodeString[0] == character[0]) {
            return i; // Found the physical keycode for this character
        }

        UCKeyTranslate(keyboardLayout, i, kUCKeyActionDisplay, shiftKey >> 8, LMGetKbdType(),
                       kUCKeyTranslateNoDeadKeysBit, &deadKeyState,
                       maxStringLength, &actualStringLength, unicodeString);

        if (unicodeString[0] == character[0]) {
            shift_required.push_back(std::string(1, character[0]));
            return i; // Found the physical keycode for this character
        }
    }
    return -1;
}

bool start_macro_async(const std::string& lua_code, pid_t& out_pid, int& out_write_fd, int& out_read_fd) {
    int parent_to_child[2]; // Main App writes, Runner reads
    int child_to_parent[2]; // Runner writes, Main App reads

    if (pipe(parent_to_child) == -1 || pipe(child_to_parent) == -1) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) { // CHILD
        dup2(parent_to_child[0], STDIN_FILENO);  // Bind stdin to Pipe A
        dup2(child_to_parent[1], STDOUT_FILENO); // Bind stdout to Pipe B
        
        // Close unused ends
        close(parent_to_child[1]);
        close(child_to_parent[0]);

        execl(get_macro_runner_path(), "macro_runner", (char*)NULL);
        _exit(1);
    } else { // PARENT
        close(parent_to_child[0]);
        close(child_to_parent[1]);

        out_pid = pid;
        out_write_fd = parent_to_child[1]; // Store this to send "TRIGGER:F5"
        out_read_fd = child_to_parent[0];  // Store this to read Runner's output

        // Send initial code
        std::string payload = lua_code + "\n--EOF--\n";
        write(out_write_fd, payload.c_str(), payload.size());
        
        return true;
    }
}
// Helper to load file into string
std::string load_file_content(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
// Robust write helper to ensure all bytes are sent
bool write_all(int fd, const void* data, size_t size) {
    const char* ptr = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t written = write(fd, ptr, size);
        if (written <= 0) return false;
        ptr += written;
        size -= written;
    }
    return true;
}

// Helper to send a length-prefixed string packet
bool send_packet(int fd, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    if (!write_all(fd, &len, sizeof(len))) return false;
    if (!write_all(fd, str.c_str(), str.size())) return false;
    return true;
}

static void start_macro_with_packets(const std::vector<std::string>& packets,
                                     std::atomic<pid_t>& out_pid,
                                     std::atomic<int>& out_write,
                                     std::atomic<int>& out_read,
                                     std::atomic<int>& log_read_pipe) {
    int p_to_c[2], c_to_p[2], log_p[2];

    if (pipe(p_to_c) == -1 || pipe(c_to_p) == -1 || pipe(log_p) == -1) return;

    pid_t pid = fork();
    if (pid == 0) { // CHILD
        dup2(p_to_c[0], STDIN_FILENO);
        dup2(c_to_p[1], 3); // Binary feedback
        dup2(log_p[1], STDOUT_FILENO);
        dup2(log_p[1], STDERR_FILENO);

        close(p_to_c[1]); close(c_to_p[0]); close(log_p[0]);

        execl(get_macro_runner_path(), "macro_runner", (char*)NULL);
        _exit(1);
    } else { // PARENT
        close(p_to_c[0]); close(c_to_p[1]); close(log_p[1]);

        out_pid.store(pid);
        out_write.store(p_to_c[1]);
        out_read.store(c_to_p[0]);
        log_read_pipe.store(log_p[0]);

        for (const auto& packet : packets) {
            if (!send_packet(p_to_c[1], packet)) {
                return;
            }
        }
    }
}

void start_macro_async_from_string(const std::string& virtual_filename,
                                   const std::string& lua_code, 
                                   std::atomic<pid_t>& out_pid, 
                                   std::atomic<int>& out_write, 
                                   std::atomic<int>& out_read, 
                                   std::atomic<int>& log_read_pipe) {
    start_macro_with_packets({virtual_filename, lua_code}, out_pid, out_write, out_read, log_read_pipe);
}

void start_project_macro_async(const std::string& virtual_filename,
                               const std::string& lua_code,
                               const std::string& project_dir,
                               const std::string& sandbox_profile,
                               const std::string& manifest_raw,
                               const std::string& permission_grants_raw,
                               std::atomic<pid_t>& out_pid,
                               std::atomic<int>& out_write,
                               std::atomic<int>& out_read,
                               std::atomic<int>& log_read_pipe) {
    start_macro_with_packets(
        {virtual_filename, lua_code, project_dir, sandbox_profile, manifest_raw, permission_grants_raw},
        out_pid,
        out_write,
        out_read,
        log_read_pipe
    );
}
// Logic to start the runner and send the initial script
void start_macro_async_from_file(const std::string& path, std::atomic<pid_t>& out_pid, std::atomic<int>& out_write, std::atomic<int>& out_read, std::atomic<int>& log_read_pipe) {
    int p_to_c[2]; // Binary Commands
    int c_to_p[2]; // Binary Feedback
    int log_p[2];  // Text Logs (stdout/stderr)

    if (pipe(p_to_c) == -1 || pipe(c_to_p) == -1 || pipe(log_p) == -1) return;

    pid_t pid = fork();
    if (pid == 0) { // CHILD
        dup2(p_to_c[0], STDIN_FILENO);  // stdin = Binary In
        dup2(c_to_p[1], 3);             // fd 3 = Binary Out (Custom FD)
        dup2(log_p[1], STDOUT_FILENO);  // stdout = Logs
        dup2(log_p[1], STDERR_FILENO);  // stderr = Logs

        // Close all unneeded ends
        close(p_to_c[1]); close(c_to_p[0]); close(log_p[0]);

        execl(get_macro_runner_path(), "macro_runner", (char*)NULL);
        _exit(1);
    } else { // PARENT
        close(p_to_c[0]); close(c_to_p[1]); close(log_p[1]);

        out_pid = pid;
        out_write = p_to_c[1];  // We write binary here
        out_read = c_to_p[0];   // We read binary here
        log_read_pipe = log_p[0]; // We read text logs here

        // Send Lua code + EOF as before...
        uint32_t len = static_cast<uint32_t>(path.size());
        write_all(out_write, &len, sizeof(len));
        write_all(out_write, path.c_str(), path.size());

        std::string code = load_file_content(path);
        len = static_cast<uint32_t>(code.size());
        write_all(out_write, &len, sizeof(len));
        write_all(out_write, code.c_str(), code.size());
    }
}

KeyCombo parse_string_to_keybind(std::string input) {
    KeyCombo combo;
    combo.count = 0;

    auto tokens = split_string(input, '+');

    for(const auto & token : tokens) {
        std::string lower_token = token;
        for (auto & c: lower_token) c = std::tolower(c);

        if (lower_token == "cmd" || lower_token == "command" || lower_token == "⌘") {
            combo.flags |= kCGEventFlagMaskCommand;
        } else if (lower_token == "shift" || lower_token == "⇧") {
            combo.flags |= kCGEventFlagMaskShift;
        } else if (lower_token == "alt" || lower_token == "option" || lower_token == "⌥") {
            combo.flags |= kCGEventFlagMaskAlternate;
        } else if (lower_token == "ctrl" || lower_token == "control" || lower_token == "⌃") {
            combo.flags |= kCGEventFlagMaskControl;
        } else {
            if (key_map.count(lower_token)) {
                combo.keycodes[combo.count++]=key_map.at(lower_token);
            } else {
                combo.keycodes[0] = 0xFFFF; // Invalid keycode
                return combo;
            }
        }
    }
    if(tokens.size() == 1){
        if(combo.count == 0){
            std::string lower_token = tokens[0];
            for (auto & c: lower_token) c = std::tolower(c);
            
            if (key_map.count(lower_token)) {
                combo.keycodes[combo.count++]=key_map.at(lower_token);
            } else {
                combo.keycodes[0] = 0xFFFF; // Invalid keycode
                return combo;
            }
        }
    }

    return combo;
}

ProcessStats get_child_stats(pid_t pid, uint64_t& last_cpu_time, long long interval) {
    struct proc_taskinfo pti;
    if (sizeof(pti) == proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti))) {
        
        // --- RAM ---
        // pti_resident_size is the physical RAM the process is actually occupying.
        size_t ram = (size_t)pti.pti_resident_size;

        // --- CPU ---
        // pti_total_user + pti_total_system is the total CPU nanoseconds used since start.
        uint64_t current_cpu_time = pti.pti_total_user + pti.pti_total_system;
        
        // We need a delta to calculate % usage. 
        // If your monitor runs every 100ms, the math is:
        // (delta_cpu_nanoseconds / interval_nanoseconds) * 100
        double cpu_usage = 0;
        if (last_cpu_time > 0) {
            uint64_t delta = current_cpu_time - last_cpu_time;
            cpu_usage = delta*ticks_to_ns_factor()*100.0/interval;

            // 100ms interval = 100,000,000 nanoseconds
        }
        last_cpu_time = current_cpu_time;

        return { cpu_usage, ram, (size_t) pti.pti_virtual_size, current_cpu_time };
    }
    return { 0, 0, 0, 0 };
}

#endif