/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

// lua_ls.h
#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <functional>
#include <queue>
#include <mutex>

class LuaLSManager {
public:
    LuaLSManager();
    ~LuaLSManager();
    
    void start(const std::string& lsp_path);
    void stop();
    void send_message(const std::string& json_message);
    void set_message_callback(std::function<void(const std::string&)> callback);
    
private:
    void lsp_thread_func(const std::string& lsp_path);
    void read_output();
    void write_input();
    
    std::thread lsp_thread;
    std::thread read_thread;
    std::thread write_thread;
    
    std::function<void(const std::string&)> message_callback;
    
    std::queue<std::string> outgoing_messages;
    std::mutex outgoing_mutex;
    
    std::atomic<bool> running = false;
    
    int input_pipe[2];
    int output_pipe[2];
    pid_t pid;
};