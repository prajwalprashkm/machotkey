// lua_lsp_manager.cpp
#include "lua_ls.h"
#include <iostream>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

LuaLSManager::LuaLSManager() {}

LuaLSManager::~LuaLSManager() {
    stop();
}

void LuaLSManager::start(const std::string& lsp_path) {
    running = true;
    lsp_thread = std::thread(&LuaLSManager::lsp_thread_func, this, lsp_path);
}

void LuaLSManager::stop() {
    if (!running) return;
    running = false;
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
    
    if (lsp_thread.joinable()) lsp_thread.join();
    if (read_thread.joinable()) read_thread.join();
    if (write_thread.joinable()) write_thread.join();
}

void LuaLSManager::lsp_thread_func(const std::string& lsp_path) {
    // Create pipes
    pipe(input_pipe);
    pipe(output_pipe);
    
    pid = fork();
    
    if (pid == 0) {
        // Child process
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        
        execlp(lsp_path.c_str(), lsp_path.c_str(), "--stdio", nullptr);
        exit(1);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);

    // Start read/write threads
    read_thread = std::thread(&LuaLSManager::read_output, this);
    write_thread = std::thread(&LuaLSManager::write_input, this);
}

void LuaLSManager::read_output() {
    char buffer[4096];
    std::string message_buffer;
    int content_length = 0;
    bool reading_headers = true;
    
    while (running) {
        ssize_t bytes_read = read(output_pipe[0], buffer, sizeof(buffer));
        if (bytes_read <= 0) break;
        
        message_buffer.append(buffer, bytes_read);
        
        while (true) {
            if (reading_headers) {
                size_t header_end = message_buffer.find("\r\n\r\n");
                if (header_end == std::string::npos) break;
                
                std::string headers = message_buffer.substr(0, header_end);
                size_t cl_pos = headers.find("Content-Length: ");
                if (cl_pos != std::string::npos) {
                    content_length = std::stoi(headers.substr(cl_pos + 16));
                }
                
                message_buffer = message_buffer.substr(header_end + 4);
                reading_headers = false;
            } else {
                if (message_buffer.length() >= content_length) {
                    std::string json_message = message_buffer.substr(0, content_length);
                    message_buffer = message_buffer.substr(content_length);
                    reading_headers = true;
                    
                    if (message_callback) {
                        message_callback(json_message);
                    }
                } else {
                    break;
                }
            }
        }
    }
}

void LuaLSManager::write_input() {
    while (running) {
        std::string message;
        
        {
            std::lock_guard<std::mutex> lock(outgoing_mutex);
            if (outgoing_messages.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            message = outgoing_messages.front();
            outgoing_messages.pop();
        }
        
        std::string full_message = "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n" + message;
     
        write(input_pipe[1], full_message.c_str(), full_message.length());
    }
}

void LuaLSManager::send_message(const std::string& json_message) {
    std::lock_guard<std::mutex> lock(outgoing_mutex);
    outgoing_messages.push(json_message);
}

void LuaLSManager::set_message_callback(std::function<void(const std::string&)> callback) {
    message_callback = callback;
}