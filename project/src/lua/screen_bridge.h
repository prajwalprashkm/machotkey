#include "sol/forward.hpp"
#include "sol/sol.hpp"
#include "input_interface.h"
#include "sol/types.hpp"
#include "sol/variadic_args.hpp"
#include <cstddef>
#include <cstdint>
#include "shared.h"
#include "ipc_protocol.h"
#include "utils.h"
#include "shm.h"
#include <QuartzCore/CABase.h>
#include "ocr.h"
#include "img_utils.h"

extern sol::protected_function capture_callback;
extern bool using_callback;
extern bool capturer_running;

extern RunnerSHM runner_shm;
extern RunnerState* g_runner_state;
extern FastOCR fast_ocr, regular_ocr;

extern "C" {
    typedef struct {
        char text[128];
        float confidence;
        double x, y, w, h;
    } LuaOCRResult;

    typedef struct {
        LuaOCRResult* data;
        size_t size;
    } OCRVector;

    inline OCRVector to_lua_results(std::vector<OCRResult> results){
        OCRVector ret;
        ret.size = results.size();
        ret.data = static_cast<LuaOCRResult*>(malloc(ret.size*sizeof(LuaOCRResult)));
        size_t i = 0;
        LuaOCRResult* dest;
        for(const auto& result : results){
            dest = ret.data + i;
            *dest = {"", result.confidence, result.boundingBox.origin.x, result.boundingBox.origin.y, result.boundingBox.size.width, result.boundingBox.size.height};
            std::strncpy(dest->text, result.text.c_str(), 128);
            i += 1;
        }
        return ret;
    }

    OCRVector _recognize_text(bool fast, uint8_t* buffer, uint32_t width, uint32_t height, uint32_t stride, bool using_region, size_t x, size_t y, size_t w, size_t h){
        if(using_region){
            if(fast){
                return to_lua_results( fast_ocr.recognize_text_in_region(buffer, width,
                                                                                height, stride, 
                                                                                x, y, w, h
                                                                                ) );
            }else{
                return to_lua_results( regular_ocr.recognize_text_in_region(buffer, width,
                                                                                height, stride, 
                                                                                x, y, w, h
                                                                                ) );

            }
        }
        
        if(fast) return to_lua_results( fast_ocr.recognize_text(buffer, width, height, stride) );
        return to_lua_results( regular_ocr.recognize_text(buffer, width, height, stride) );
    }

    void _free_OCRVector(OCRVector vector){
        if(vector.data != nullptr){
            free(vector.data);
            vector.data = nullptr;
        }
    }

    typedef struct {
        unsigned int id;
        double x, y, w, h;
        double scale, refresh_rate;
        bool is_main;
    } LuaDisplay;
}

#define THROW_LUA_ERROR(s, msg) throw sol::error(get_current_location(s)+msg)

inline auto send_draw_command(MsgType type, DrawCommand cmd) {
    auto id = current_response_id.fetch_add(1);
    IPCHeader header = { type, sizeof(cmd), id+1};
    
#ifdef IPC_DEBUG 
    fprintf(stderr, "[IPC] Thread %p sending draw command ID=%llu type=%d\n", 
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

    ssize_t n2 = write(BINARY_OUT_FD, &cmd, sizeof(cmd));

#ifdef IPC_DEBUG 
    fprintf(stderr, "[IPC] Thread %p wrote %zd/%zu payload bytes (ID=%llu)\n", 
            (void*)pthread_self(), n2, sizeof(cmd), (unsigned long long)id);
    
    if (n2 != sizeof(cmd)) {
        fprintf(stderr, "[IPC ERROR] Thread %p PARTIAL PAYLOAD WRITE! Expected %zu, got %zd (ID=%llu)\n",
                (void*)pthread_self(), sizeof(cmd), n2, (unsigned long long)id);
    }
#endif

    ipc_mutex.unlock();
    
#ifdef IPC_DEBUG 
    fprintf(stderr, "[IPC] Thread %p waiting for response ID=%llu\n", 
            (void*)pthread_self(), (unsigned long long)id);
#endif
    
    return header.request_id;
}

class ScreenBridge {
public:
    static void register_code(sol::state& lua, sol::table& system) {
        sol::table screen = lua.create_table();
        
        screen["get_dimensions"] = [](){
            ipc_mutex.lock();
            current_response_id.fetch_add(1);
            IPCHeader header = { MsgType::SYSTEM_SCREEN_REQUEST_DIM, 0, current_response_id };
            write(BINARY_OUT_FD, &header, sizeof(header));
            ipc_mutex.unlock();

            response_manager.wait_for_response(header.request_id);
            return std::make_tuple(screen_width, screen_height);
        };

        screen["begin_capture"] = [](const sol::variadic_args& args) {
            if (!has_runtime_permission("screen_capture")) {
                abort_for_permission_violation("screen_capture", "system.screen.begin_capture");
            }
            if(capturer_running) {
                throw sol::error("Screen capture is already running.");
            }
 
            size_t nargs = args.size();
 
            if(nargs > 2){
                throw sol::error("Invalid number of arguments to system.screen.begin_capture (0-2 expected)");
            }
 
            ScreenCaptureRequest req;
 
            req.fps        = 60;
            req.x = req.y = req.w = req.h = 0;
            req.display_id = CGMainDisplayID();
            using_callback = false;
 
            sol::optional<sol::protected_function> callback_arg;
            sol::optional<sol::table>              options_arg;
 
            for(size_t i = 0; i < nargs; i++){
                if(args[i].is<sol::protected_function>()){
                    if(callback_arg){
                        throw sol::error("system.screen.begin_capture: only one callback argument is allowed");
                    }
                    callback_arg = args[i].as<sol::protected_function>();
                } else if(args[i].is<sol::table>()){
                    if(options_arg){
                        throw sol::error("system.screen.begin_capture: only one options table is allowed");
                    }
                    options_arg = args[i].as<sol::table>();
                } else {
                    throw sol::error("system.screen.begin_capture: arguments must be a callback function and/or an options table");
                }
            }
 
            if(callback_arg){
                capture_callback = *callback_arg;
                using_callback   = true;
            }
 
            if(options_arg){
                sol::table opts = *options_arg;
 
                auto fps_val = opts.get<sol::optional<int>>("fps");
                if(fps_val){
                    if(*fps_val <= 0){
                        throw sol::error("system.screen.begin_capture: options.fps must be a positive integer");
                    }
                    req.fps = *fps_val;
                }
 
                auto region_val = opts.get<sol::optional<sol::table>>("region");
                if(region_val){
                    sol::table region = *region_val;
                    auto rx = region.get<sol::optional<int>>("x");
                    auto ry = region.get<sol::optional<int>>("y");
                    auto rw = region.get<sol::optional<int>>("w");
                    auto rh = region.get<sol::optional<int>>("h");
                    if(!rx || !ry || !rw || !rh){
                        throw sol::error("system.screen.begin_capture: options.region must contain x, y, w, h");
                    }
                    if(*rw <= 0 || *rh <= 0){
                        throw sol::error("system.screen.begin_capture: options.region w and h must be positive");
                    }
                    req.x = *rx;
                    req.y = *ry;
                    req.w = *rw;
                    req.h = *rh;
                }
 
                auto display_val = opts.get<sol::optional<int>>("display_id");
                if(display_val){
                    req.display_id = static_cast<CGDirectDisplayID>(*display_val);
                }
            }
 
            capturer_running = true;
 
            auto id = current_response_id.fetch_add(1);
            IPCHeader header = { MsgType::SYSTEM_SCREEN_START_CAPTURE, sizeof(ScreenCaptureRequest), id+1 };
 
            ipc_mutex.lock();
            write(BINARY_OUT_FD, &header, sizeof(header));
            write(BINARY_OUT_FD, &req, sizeof(req));
            ipc_mutex.unlock();
 
            response_manager.wait_for_response(header.request_id);
        };

        screen["stop_capture"] = []() {
            if (!has_runtime_permission("screen_capture")) {
                abort_for_permission_violation("screen_capture", "system.screen.stop_capture");
            }
            if(!capturer_running) {
                throw sol::error("No active screen capture to end.");
            }

            capturer_running = false;
            using_callback = false;

            auto id = current_response_id.fetch_add(1);
            // Send stop capture message
            IPCHeader header = { MsgType::SYSTEM_SCREEN_STOP_CAPTURE, 0, id+1 };

            ipc_mutex.lock();
            write(BINARY_OUT_FD, &header, sizeof(header));
            ipc_mutex.unlock();

            g_runner_state->shm_header = nullptr;
            g_runner_state->pixel_data = nullptr;
            runner_shm.disconnect();
        };

        screen["get_current_timestamp"] = [](){
            return static_cast<double>(CACurrentMediaTime()*1000000);
        };

        // Bind as explicit read-only properties for better compatibility across
        // older Apple clang/sol2 combinations (member-pointer binding can fail).
        lua.new_usertype<LuaDisplay>("Display",
            "id", sol::property([](const LuaDisplay& d) { return d.id; }),
            "x", sol::property([](const LuaDisplay& d) { return d.x; }),
            "y", sol::property([](const LuaDisplay& d) { return d.y; }),
            "w", sol::property([](const LuaDisplay& d) { return d.w; }),
            "h", sol::property([](const LuaDisplay& d) { return d.h; }),
            "scale", sol::property([](const LuaDisplay& d) { return d.scale; }),
            "refresh_rate", sol::property([](const LuaDisplay& d) { return d.refresh_rate; }),
            "is_main", sol::property([](const LuaDisplay& d) { return d.is_main; })
        );

        screen["get_displays"] = [](){
            CGDirectDisplayID display_ids[16];
            uint32_t display_count;
            CGGetActiveDisplayList(16, display_ids, &display_count);

            std::vector<LuaDisplay> displays;
            displays.reserve(display_count);

            for(uint32_t i = 0; i < display_count; i++){
                CGDirectDisplayID id = display_ids[i];
                CGDisplayModeRef mode = CGDisplayCopyDisplayMode(id);
                if(mode){
                    CGRect bounds = CGDisplayBounds(id);
                    displays.push_back({
                        .id = id,
                        .x = static_cast<double>(bounds.origin.x),
                        .y = static_cast<double>(bounds.origin.y),
                        .w = static_cast<double>(bounds.size.width),
                        .h = static_cast<double>(bounds.size.height),
                        .scale = static_cast<double>(CGDisplayModeGetPixelWidth(mode)) / CGDisplayModeGetWidth(mode),
                        .refresh_rate = CGDisplayModeGetRefreshRate(mode),
                        .is_main = (id == CGMainDisplayID())
                    });
                    CGDisplayModeRelease(mode);
                }else{
                    CGRect bounds = CGDisplayBounds(id);
                    displays.push_back({
                        .id = id,
                        .x = static_cast<double>(bounds.origin.x),
                        .y = static_cast<double>(bounds.origin.y),
                        .w = static_cast<double>(bounds.size.width),
                        .h = static_cast<double>(bounds.size.height),
                        .scale = -1.0,
                        .refresh_rate = -1.0,
                        .is_main = (id == CGMainDisplayID())
                    });
                }
            }
            return sol::as_table(displays);
        };

        screen["get_main_display"] = [](){
            LuaDisplay ret;
            ret.is_main = true;

            CGRect bounds = CGDisplayBounds(CGMainDisplayID());
            ret.x = static_cast<double>(bounds.origin.x);
            ret.y = static_cast<double>(bounds.origin.y);
            ret.w = static_cast<double>(bounds.size.width);
            ret.h = static_cast<double>(bounds.size.height);

            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
            if(mode){
                ret.scale = static_cast<double>(CGDisplayModeGetPixelWidth(mode)) / CGDisplayModeGetWidth(mode);
                ret.refresh_rate = CGDisplayModeGetRefreshRate(mode);
            }else{
                ret.scale = -1.0;
                ret.refresh_rate = -1.0;
            }
            
            return ret;
        };

        screen["get_display"] = [](uint32_t id){
            LuaDisplay ret;
            ret.is_main = id == CGMainDisplayID();

            CGRect bounds = CGDisplayBounds(id);
            ret.x = static_cast<double>(bounds.origin.x);
            ret.y = static_cast<double>(bounds.origin.y);
            ret.w = static_cast<double>(bounds.size.width);
            ret.h = static_cast<double>(bounds.size.height);

            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(id);
            if(mode){
                ret.scale = static_cast<double>(CGDisplayModeGetPixelWidth(mode)) / CGDisplayModeGetWidth(mode);
                ret.refresh_rate = CGDisplayModeGetRefreshRate(mode);
            }else{
                ret.scale = -1.0;
                ret.refresh_rate = -1.0;
            }
            
            return ret;
        };

        /*lua.new_usertype<LuaOCRResult>("OCRResult",
            "text", &LuaOCRResult::text,
            "confidence", &LuaOCRResult::confidence,
            "x", &LuaOCRResult::x,
            "y", &LuaOCRResult::y,
            "w", &LuaOCRResult::w,
            "h", &LuaOCRResult::h
        );

        auto ocr = lua.create_table();
        ocr["fast"] = lua.create_table();

        ocr["fast"]["_recognize_text"] = [](uintptr_t buffer, uint32_t width, uint32_t height, uint32_t stride, const std::optional<sol::table>& options){
            uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer);
            if(options.has_value()){
                bool using_region = false;

                size_t x, y, w, h;

                auto value = options.value();
                if(value["region"].is<sol::table>()){
                    auto region = value.get<sol::table>("region");

                    x = region.get<size_t>("x");
                    y = region.get<size_t>("y");
                    w = region.get<size_t>("w");
                    h = region.get<size_t>("h");
                    using_region = true;
                }

                if(using_region){
                    return to_lua_results( fast_ocr.recognize_text_in_region(ptr, width,
                                                                                        height, stride, 
                                                                                        x, y, w, h
                                                                                     ) );
                }
            }
                
            return to_lua_results( fast_ocr.recognize_text(ptr, width, height, stride) );
        };

        ocr["accurate"] = lua.create_table();

        ocr["accurate"]["_recognize_text"] = [](uintptr_t buffer, uint32_t width, uint32_t height, uint32_t stride, const std::optional<sol::table>& options){
            uint8_t* ptr = reinterpret_cast<uint8_t*>(buffer);
            if(options.has_value()){
                bool using_region = false;

                size_t x, y, w, h;

                auto value = options.value();
                if(value["region"].is<sol::table>()){
                    auto region = value.get<sol::table>("region");

                    x = region.get<size_t>("x");
                    y = region.get<size_t>("y");
                    w = region.get<size_t>("w");
                    h = region.get<size_t>("h");
                    using_region = true;
                }

                if(using_region){
                    return to_lua_results( regular_ocr.recognize_text_in_region(ptr, width,
                                                                                        height, stride, 
                                                                                        x, y, w, h
                                                                                     ) );
                }
            }
                
            return to_lua_results( regular_ocr.recognize_text(ptr, width, height, stride) );
        };

        ocr["combine"] = [](const std::vector<LuaOCRResult>& results){
            std::string combined;
            for(const auto& result : results){
                if(!combined.empty()) combined += " ";
                combined += result.text;
            }
            return combined;
        };

        screen["ocr"] = ocr;*/

        auto canvas = lua.create_table();

        canvas["rect"] = [](sol::table rect, uint32_t color, std::optional<sol::table> options) {
            //std::cout << "Drawing rect with ID: " << id << " at (" << x << ", " << y << ") size (" << w << "x" << h << ") color: " << std::hex << color << std::dec << std::endl;
            DrawCommand cmd = {DrawCmdType::RECT, rect.get<float>("x"), rect.get<float>("y"), rect.get<float>("w"), rect.get<float>("h"), color, 1.0f, ""};
            if(options){
                sol::table opts = options.value();
                if(opts["id"].is<std::string>()){
                    std::string id_str = opts.get<std::string>("id");
                    std::strncpy(cmd.id, id_str.c_str(), sizeof(cmd.id));
                }else{
                    std::strncpy(cmd.id, "", sizeof(cmd.id));
                }
                if(opts["thickness"].is<float>()){
                    cmd.thickness = opts.get<float>("thickness");
                }
                if(opts["classes"].is<sol::table>()){
                    sol::table classes = opts.get<sol::table>("classes");
                    size_t class_count = 0;
                    for(auto& pair : classes){
                        if(class_count >= 8) break;
                        if(pair.second.is<std::string>()){
                            std::string class_name = pair.second.as<std::string>();
                            std::strncpy(cmd.classes[class_count], class_name.c_str(), 32);
                            //std::cout << "Adding class: " << class_name << " to draw command." << std::endl;
                            class_count++;
                        }
                    }
                    cmd.class_count = static_cast<uint32_t>(class_count);
                }
                if(opts["fill"].is<int>()){
                    cmd.fill_color = opts.get<uint32_t>("fill");
                    cmd.fill = true;
                }else{
                    cmd.fill = false;
                }
            }else{
                std::strncpy(cmd.id, "", sizeof(cmd.id));
                cmd.class_count = 0;
            }

            send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
        };

        canvas["text"] = [](std::string text, float x, float y, uint32_t color, std::optional<sol::table> options) {
            //std::cout << "Drawing rect with ID: " << id << " at (" << x << ", " << y << ") size (" << w << "x" << h << ") color: " << std::hex << color << std::dec << std::endl;
            DrawCommand cmd = {DrawCmdType::TEXT, x, y, 0, 0, color, 1.0f};
            std::strncpy(cmd.text, text.c_str(), sizeof(cmd.text));
            if(options){
                sol::table opts = options.value();
                if(opts["id"].is<std::string>()){
                    std::string id_str = opts.get<std::string>("id");
                    std::strncpy(cmd.id, id_str.c_str(), sizeof(cmd.id));
                }else{
                    std::strncpy(cmd.id, "", sizeof(cmd.id));
                }
                if(opts["thickness"].is<float>()){
                    cmd.thickness = opts.get<float>("thickness");
                }
                if(opts["classes"].is<sol::table>()){
                    sol::table classes = opts.get<sol::table>("classes");
                    size_t class_count = 0;
                    for(auto& pair : classes){
                        if(class_count >= 8) break;
                        if(pair.second.is<std::string>()){
                            std::string class_name = pair.second.as<std::string>();
                            std::strncpy(cmd.classes[class_count], class_name.c_str(), 32);
                            //std::cout << "Adding class: " << class_name << " to draw command." << std::endl;
                            class_count++;
                        }
                    }
                    cmd.class_count = static_cast<uint32_t>(class_count);
                }
                if(opts["fill"].is<int>()){
                    cmd.fill_color = opts.get<uint32_t>("fill");
                    cmd.fill = true;
                }else{
                    cmd.fill = false;
                }
            }else{
                std::strncpy(cmd.id, "", sizeof(cmd.id));
                cmd.class_count = 0;
            }

            send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
        };

        canvas["line"] = [](float x1, float y1, float x2, float y2, uint32_t color, std::optional<sol::table> options) {
            DrawCommand cmd = {DrawCmdType::LINE, x1, y1, x2, y2, color, 1.0f, ""};
            if(options){
                sol::table opts = options.value();
                if(opts["id"].is<std::string>()){
                    std::string id_str = opts.get<std::string>("id");
                    std::strncpy(cmd.id, id_str.c_str(), sizeof(cmd.id));
                }else{
                    std::strncpy(cmd.id, "", sizeof(cmd.id));
                }
                if(opts["thickness"].is<float>()){
                    cmd.thickness = opts.get<float>("thickness");
                }
                if(opts["classes"].is<sol::table>()){
                    sol::table classes = opts.get<sol::table>("classes");
                    size_t class_count = 0;
                    for(auto& pair : classes){
                        if(class_count >= 8) break;
                        if(pair.second.is<std::string>()){
                            std::string class_name = pair.second.as<std::string>();
                            std::strncpy(cmd.classes[class_count], class_name.c_str(), 32);
                            //std::cout << "Adding class: " << class_name << " to draw command." << std::endl;
                            class_count++;
                        }
                    }
                    cmd.class_count = static_cast<uint32_t>(class_count);
                }
            }else{
                std::strncpy(cmd.id, "", sizeof(cmd.id));
                cmd.class_count = 0;
            }

            send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
        };

        canvas["remove_by_id"] = [](std::string id){
            DrawCommand cmd = {DrawCmdType::REMOVE, 0, 0, 0, 0, 0, 0.0f, ""};
            std::strncpy(cmd.id, id.c_str(), sizeof(cmd.id));
            send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
        };

        canvas["remove_by_classes"] = [](sol::table classes){
            DrawCommand cmd = {DrawCmdType::REMOVE, 0, 0, 0, 0, 0, 0.0f, ""};
            
            for(auto& pair : classes){
                if(cmd.class_count >= 8) break;
                if(pair.second.is<std::string>()){
                    std::string class_name = pair.second.as<std::string>();
                    std::strncpy(cmd.classes[cmd.class_count], class_name.c_str(), 32);
                    //std::cout << "Adding class: " << class_name << " to remove draw command." << std::endl;
                    cmd.class_count++;
                }
            }

            std::strncpy(cmd.id, "", sizeof(cmd.id));
            send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
        };

        canvas["clear"] = [](){
            DrawCommand cmd = {DrawCmdType::CLEAR, 0, 0, 0, 0, 0, 0.0f, ""};
            std::strncpy(cmd.id, "", sizeof(cmd.id));
            cmd.class_count = 0;
            auto id = send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
            response_manager.wait_for_response(id);
        };

        canvas["set_display"] = [](uint32_t display_id){
            DrawCommand cmd = {DrawCmdType::CLEAR, 0, 0, 0, 0, 0, 0.0f, ""};
            std::strncpy(cmd.id, "", sizeof(cmd.id));
            cmd.class_count = 0;
            auto id = send_draw_command(MsgType::SYSTEM_SCREEN_CANVAS_DRAW_COMMAND, cmd);
            response_manager.wait_for_response(id);

            id = current_response_id.fetch_add(1);
            IPCHeader header = { MsgType::SYSTEM_SCREEN_CANVAS_SET_DISPLAY, display_id,  id + 1 };
            
            ipc_mutex.lock();
            write(BINARY_OUT_FD, &header, sizeof(header));
            ipc_mutex.unlock();

            response_manager.wait_for_response(id + 1);
        };

        screen["canvas"] = canvas;

        // Add screen table to system table
        system["screen"] = screen;
    }
};
