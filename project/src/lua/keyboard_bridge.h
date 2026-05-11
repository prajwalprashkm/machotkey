/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#include "sol/sol.hpp"
#include "input_interface.h"
#include "sol/types.hpp"
#include "sol/variadic_args.hpp"
#include <cstddef>
#include <cstdint>
#include "shared.h"
#include "ipc_protocol.h"
#include "utils.h"
#include "../../include/debug_config.h"

#if MHK_ENABLE_DEBUG_LOGS
#ifndef IPC_DEBUG
#define IPC_DEBUG
#endif
#endif

#define THROW_LUA_ERROR(s, msg) throw sol::error(get_current_location(s)+msg)

inline auto send_keyboard_data(MsgType type, uint16_t keycode, uint8_t event_type, CGEventFlags flags) {
    auto id = current_response_id.fetch_add(1);
    IPCHeader header = { type, sizeof(KeyboardData), id+1};
    KeyboardData kd = {event_type, keycode, flags};

#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p sending keyboard message ID=%llu type=%d keycode=%u\n", 
            (void*)pthread_self(), (unsigned long long)id, (int)type, (unsigned)keycode);
#endif

    // Send the header then the payload
    ipc_mutex.lock();
    
    ssize_t n1 = write(BINARY_OUT_FD, &header, sizeof(header));
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu header bytes (ID=%llu)\n", 
            (void*)pthread_self(), n1, sizeof(header), (unsigned long long)id);
#endif
    
#ifdef IPC_DEBUG
    if (n1 != sizeof(header)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL HEADER WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(header), n1, (unsigned long long)id);
    }
#endif
    
    ssize_t n2 = write(BINARY_OUT_FD, &kd, sizeof(kd));

#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu payload bytes (ID=%llu)\n", 
            (void*)pthread_self(), n2, sizeof(kd), (unsigned long long)id);
#endif
    
#ifdef IPC_DEBUG
    if (n2 != sizeof(kd)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL PAYLOAD WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(kd), n2, (unsigned long long)id);
    }
#endif
    
    ipc_mutex.unlock();
    
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p waiting for response ID=%llu\n", 
            (void*)pthread_self(), (unsigned long long)id);
#endif

    return header.request_id;
}

inline auto send_string(MsgType type, const std::string& text, InputInterface::Keyboard::TypeMode mode, int interval_ms = 0) {
    uint32_t payload_size = static_cast<uint32_t>(sizeof(mode) + sizeof(interval_ms) + text.length());
    auto id = current_response_id.fetch_add(1);
    IPCHeader header = { type, payload_size, id+1};

#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p sending string message ID=%llu type=%d len=%zu\n", 
            (void*)pthread_self(), (unsigned long long)id, (int)type, text.length());
#endif

    ipc_mutex.lock();
    
    ssize_t n1 = write(BINARY_OUT_FD, &header, sizeof(header));
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu header bytes (ID=%llu)\n", 
            (void*)pthread_self(), n1, sizeof(header), (unsigned long long)id);
#endif
    
#ifdef IPC_DEBUG
    if (n1 != sizeof(header)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL HEADER WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(header), n1, (unsigned long long)id);
    }
#endif

    ssize_t n2 = write(BINARY_OUT_FD, &mode, sizeof(mode));
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu mode bytes (ID=%llu)\n", 
            (void*)pthread_self(), n2, sizeof(mode), (unsigned long long)id);
#endif
    
#ifdef IPC_DEBUG
    if (n2 != sizeof(mode)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL MODE WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(mode), n2, (unsigned long long)id);
    }
#endif

    ssize_t n3 = write(BINARY_OUT_FD, &interval_ms, sizeof(interval_ms));
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu interval bytes (ID=%llu)\n", 
            (void*)pthread_self(), n3, sizeof(interval_ms), (unsigned long long)id);
#endif
    
    if (n3 != sizeof(interval_ms)) {
#ifdef IPC_DEBUG
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL INTERVAL WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(interval_ms), n3, (unsigned long long)id);
#endif
    }

    ssize_t n4 = write(BINARY_OUT_FD, text.c_str(), text.length());
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu text bytes (ID=%llu)\n", 
            (void*)pthread_self(), n4, text.length(), (unsigned long long)id);
#endif
    
    if (n4 != (ssize_t)text.length()) {
#ifdef IPC_DEBUG
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL TEXT WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), text.length(), n4, (unsigned long long)id);
#endif
    }
    
    ipc_mutex.unlock();
    
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p waiting for response ID=%llu\n", 
            (void*)pthread_self(), (unsigned long long)id);
#endif

    return header.request_id;
}

class KeyboardBridge {
public:
    static void register_code(sol::state& lua, sol::table& system) {
        sol::table keyboard = lua.create_table();

        keyboard["press"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("keyboard_control")) {
                abort_for_permission_violation("keyboard_control", "system.keyboard.press");
            }
            size_t nargs = args.size();
            if(nargs != 1) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.keyboard.press (1 expected)");
            }
            if(!args[0].is<std::string>()){
                THROW_LUA_ERROR(s, "First argument to system.keyboard.press must be a string (key combo)");
            }
            std::string combo_str = args[0].as<std::string>();
            KeyCombo combo = parse_string_to_keybind(combo_str);
            if(combo.keycodes[0] == 0xFFFF) {
                THROW_LUA_ERROR(s, "Invalid key in key combo: " + combo_str);
            }else if(combo.count > 1){
                THROW_LUA_ERROR(s, "Multiple keys not supported in system.keyboard.press; use system.keyboard.type or system.keyboard.send instead.");
            }

            auto current_id = send_keyboard_data(MsgType::SYSTEM_KEYBOARD_PRESS, combo.keycodes[0], 0, combo.flags);
            response_manager.wait_for_response(current_id);
        };

        keyboard["send"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("keyboard_control")) {
                abort_for_permission_violation("keyboard_control", "system.keyboard.send");
            }
            size_t nargs = args.size();
            if(nargs != 2) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.keyboard.send (2 expected)");
            }
            if(!args[0].is<int>()){
                THROW_LUA_ERROR(s, "First argument to system.keyboard.send must be a key event type (system.keyboard.EventType)");
            }
            int event_val = args[0].as<int>();
            if(event_val < -2 || event_val > -1) {
                THROW_LUA_ERROR(s, "Invalid key event type value. Use system.keyboard.EventType constants.");
            }

            InputInterface::Keyboard::KeyEventType event_type = static_cast<InputInterface::Keyboard::KeyEventType>(event_val + 2);

            if(!args[1].is<std::string>()){
                THROW_LUA_ERROR(s, "Second argument to system.keyboard.send must be a string (key combo)");
            }

            std::string combo_str = args[1].as<std::string>();
            KeyCombo combo = parse_string_to_keybind(combo_str);

            if(combo.keycodes[0] == 0xFFFF) {
                THROW_LUA_ERROR(s, "Invalid key in key combo: " + combo_str);
            }else if(combo.count > 1){
                THROW_LUA_ERROR(s, "Multiple keys not supported in a single system.keyboard.send call; use several system.keyboard.send calls with system.keyboard.EventType.DOWN instead.");
            }

            auto current_id = send_keyboard_data(MsgType::SYSTEM_KEYBOARD_EVENT, combo.keycodes[0], (uint8_t) event_type, combo.flags);
            response_manager.wait_for_response(current_id);
        };
        
        keyboard["type"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("keyboard_control")) {
                abort_for_permission_violation("keyboard_control", "system.keyboard.type");
            }
            size_t nargs = args.size();
            if(nargs < 2 || nargs > 3) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.keyboard.send (2-3 expected)");
            }
            if(!args[0].is<int>()){
                THROW_LUA_ERROR(s, "First argument to system.keyboard.type must be a type mode (system.keyboard.TypeMode)");
            }
            int mode_val = args[0].as<int>();
            if(mode_val < -2 || mode_val > -1) {
                THROW_LUA_ERROR(s, "Invalid key event type value. Use system.keyboard.TypeMode constants.");
            }

            InputInterface::Keyboard::TypeMode type_mode = static_cast<InputInterface::Keyboard::TypeMode>(mode_val + 2);

            if(type_mode == InputInterface::Keyboard::TypeMode::STRING && nargs != 2){
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.keyboard.send when using STRING mode (2 expected)");
            }

            if(!args[1].is<std::string>()){
                THROW_LUA_ERROR(s, "Second argument to system.keyboard.send must be a string (text to type)");  
            }

            std::string msg_str = args[1].as<std::string>();

            int interval_ms = 0;
            if(type_mode == InputInterface::Keyboard::TypeMode::SEQUENTIAL && nargs == 3){
                if(!args[2].is<int>()){
                    THROW_LUA_ERROR(s, "Third argument to system.keyboard.send must be an integer (interval in milliseconds)");
                }
                interval_ms = args[2].as<int>();
            }

            auto current_id = send_string(MsgType::SYSTEM_KEYBOARD_TYPE, msg_str, type_mode, interval_ms);

            response_manager.wait_for_response(current_id);
        };

        keyboard["EventType"] = lua.create_table_with(
            "DOWN", -2,
            "UP", -1
        );

        keyboard["TypeMode"] = lua.create_table_with(
            "SEQUENTIAL", -2,
            "STRING", -1
        );

        system["keyboard"] = keyboard;
    }
};