#include "sol/sol.hpp"
#include "input_interface.h"
#include "sol/types.hpp"
#include "sol/variadic_args.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "shared.h"
#include "ipc_protocol.h"
#include "utils.h"

#define THROW_LUA_ERROR(s, msg) throw sol::error(get_current_location(s)+msg)

inline auto send_mouse_data(MsgType type, double x, double y, uint8_t button, uint8_t event_type, double duration, int clicks) {
    auto id = current_response_id.fetch_add(1);
    IPCHeader header = { type, sizeof(MouseData), id+1};
    MouseData md = {x, y, button, event_type, duration, clicks};
    
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p sending mouse message ID=%llu type=%d\n", 
            (void*)pthread_self(), (unsigned long long)id, (int)type);
#endif

    ipc_mutex.lock();
    
    ssize_t n1 = write(BINARY_OUT_FD, &header, sizeof(header));
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu header bytes (ID=%llu)\n", 
            (void*)pthread_self(), n1, sizeof(header), (unsigned long long)id);
    
    if (n1 != sizeof(header)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL HEADER WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(header), n1, (unsigned long long)id);
    }
#endif

    ssize_t n2 = write(BINARY_OUT_FD, &md, sizeof(md));

#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu payload bytes (ID=%llu)\n", 
            (void*)pthread_self(), n2, sizeof(md), (unsigned long long)id);
    
    if (n2 != sizeof(md)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL PAYLOAD WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(md), n2, (unsigned long long)id);
    }
#endif
    
    ipc_mutex.unlock();
    
#ifdef IPC_DEBUG
    fprintf(stderr, "[IPC] Thread %p waiting for response ID=%llu\n", 
            (void*)pthread_self(), (unsigned long long)id);
#endif

    return header.request_id;
}

extern std::atomic<MousePosition> current_mouse_position;

class MouseBridge {
public:
    static void register_code(sol::state& lua, sol::table& system) {
        sol::table mouse = lua.create_table();
        mouse["move"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("mouse_control")) {
                abort_for_permission_violation("mouse_control", "system.mouse.move");
            }
            size_t nargs = args.size();
            if(nargs < 2 || nargs > 3) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.mouse.move (2-3 expected)");
            }
            double x, y;
            if(!args[0].is<double>() || !args[1].is<double>()){
                THROW_LUA_ERROR(s, "First two arguments to system.mouse.move must be numbers (x, y)");
            }
            x = args[0].as<double>();
            y = args[1].as<double>();

            if(nargs == 3){
                if(!args[2].is<double>()){
                    THROW_LUA_ERROR(s, "Third argument to system.mouse.move must be a number (duration)");
                }
                double dur = args[2].as<double>();

                auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_MOVE, x, y, 0, 0, dur, 0);
                response_manager.wait_for_response(current_id);
            }else{
                auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_MOVE, x, y, 0, 0, 0.0, 0);
                response_manager.wait_for_response(current_id);
            }
        };

        mouse["click"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("mouse_control")) {
                abort_for_permission_violation("mouse_control", "system.mouse.click");
            }
            size_t nargs = args.size();

            if(nargs < 1 || nargs > 4) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.mouse.click (1-4 expected)");
            }

            InputInterface::Mouse::MouseButton button;
            if(!args[0].is<int>()) {
                THROW_LUA_ERROR(s, "First argument to system.mouse.click must be a mouse button (system.mouse.Button)");
            }else{
                int btn_val = args[0].as<int>();
                if(btn_val < -3 || btn_val > -1) {
                    THROW_LUA_ERROR(s, "Invalid mouse button value. Use system.mouse.Button constants.");
                }
                button = static_cast<InputInterface::Mouse::MouseButton>(btn_val + 3);
                
                if(nargs == 1){
                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_CLICK, -1, -1, (uint8_t) button, 0, -1.0, 1);
                    response_manager.wait_for_response(current_id);
                }else if(nargs == 2){
                    if(!args[1].is<int>()){
                        THROW_LUA_ERROR(s, "Second argument to system.mouse.click must be an integer (number of clicks)");
                    }
                    int clicks = args[1].as<int>();
                    
                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_CLICK, -1, -1, (uint8_t) button, 0, -1.0, clicks);
                    response_manager.wait_for_response(current_id);
                }else if(nargs == 3){
                    if(!args[1].is<double>() || !args[2].is<double>()){
                        THROW_LUA_ERROR(s, "Second and third arguments to system.mouse.click must be numbers (x, y)");
                    }
                    double x = args[1].as<double>();
                    double y = args[2].as<double>();

                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_CLICK, x, y, (uint8_t) button, 0, -1.0, 1);
                    response_manager.wait_for_response(current_id);
                }else if(nargs == 4){
                    if(!args[1].is<double>() || !args[2].is<double>()){
                        THROW_LUA_ERROR(s, "Second and third arguments to system.mouse.click must be numbers (x, y)");
                    }else if(!args[3].is<int>()){
                        THROW_LUA_ERROR(s, "Fourth argument to system.mouse.click must be an integer (number of clicks)");
                    }
                    double x = args[1].as<double>();
                    double y = args[2].as<double>();
                    int clicks = args[3].as<int>();

                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_CLICK, x, y, (uint8_t) button, 0, -1.0, clicks);
                    response_manager.wait_for_response(current_id);
                }
            }
        };

        mouse["send"] = [](sol::variadic_args args, sol::this_state s){
            if (!has_runtime_permission("mouse_control")) {
                abort_for_permission_violation("mouse_control", "system.mouse.send");
            }
            size_t nargs = args.size();

            if(nargs != 2 && nargs != 4) {
                THROW_LUA_ERROR(s, "Invalid number of arguments to system.mouse.send (2 or 4 expected)");
            }

            InputInterface::Mouse::MouseButton button;
            if(!args[0].is<int>()) {
                THROW_LUA_ERROR(s, "First argument to system.mouse.send must be a mouse button (system.mouse.Button)");
            }else{
                int btn_val = args[0].as<int>();
                if(btn_val < -3 || btn_val > -1) {
                    THROW_LUA_ERROR(s, "Invalid mouse button value. Use system.mouse.Button constants.");
                }
                button = static_cast<InputInterface::Mouse::MouseButton>(btn_val + 3);
                
                if(nargs == 2){
                    if(!args[1].is<int>()){
                        THROW_LUA_ERROR(s, "Second argument to system.mouse.send must be a mouse event type (system.mouse.EventType)");
                    }
                    int event_val = args[1].as<int>();
                    if(event_val < -2 || event_val > -1) {
                        THROW_LUA_ERROR(s, "Invalid mouse event type value. Use system.mouse.EventType constants.");
                    }
                    InputInterface::Mouse::MouseEventType event_type = static_cast<InputInterface::Mouse::MouseEventType>(event_val + 2);

                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_EVENT, -1, -1, (uint8_t) button, (uint8_t) event_type, -1.0, 0);
                    response_manager.wait_for_response(current_id);
                }else if(nargs == 4){
                    if(!args[1].is<int>()){
                        THROW_LUA_ERROR(s, "Second argument to system.mouse.send must be a mouse event type (system.mouse.EventType)");
                    }
                    int event_val = args[1].as<int>();
                    if(event_val < -2 || event_val > -1) {
                        THROW_LUA_ERROR(s, "Invalid mouse event type value. Use system.mouse.EventType constants.");
                    }

                    InputInterface::Mouse::MouseEventType event_type = static_cast<InputInterface::Mouse::MouseEventType>(event_val + 2);
                    if(!args[2].is<double>() || !args[3].is<double>()){
                        THROW_LUA_ERROR(s, "Third and fourth arguments to system.mouse.click must be numbers (x, y)");
                    }

                    double x = args[2].as<double>();
                    double y = args[3].as<double>();

                    auto current_id = send_mouse_data(MsgType::SYSTEM_MOUSE_EVENT, x, y, (uint8_t) button, (uint8_t) event_type, -1.0, 0);
                    response_manager.wait_for_response(current_id);
                }
            }
        };

        mouse["get_position"] = []() -> std::pair<double, double> {
            if (!has_runtime_permission("mouse_control")) {
                abort_for_permission_violation("mouse_control", "system.mouse.get_position");
            }
            auto id = current_response_id.fetch_add(1);
            IPCHeader header = { MsgType::SYSTEM_MOUSE_REQUEST_POSITION, 0, id+1};

            ipc_mutex.lock();
            write(BINARY_OUT_FD, &header, sizeof(header));
            ipc_mutex.unlock();

            response_manager.wait_for_response(header.request_id);
            
            auto pos = current_mouse_position.load();
            return {pos.x, pos.y};
        };

        mouse["Button"] = lua.create_table_with(
            "LEFT", -3,
            "RIGHT", -2,
            "MIDDLE", -1
        );

        mouse["EventType"] = lua.create_table_with(
            "DOWN", -2,
            "UP", -1
        );

        system["mouse"] = mouse;
    }
};

