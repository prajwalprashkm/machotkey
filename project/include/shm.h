#pragma once
#include <atomic>
#include <cstdint>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include "shared.h"
#include <iostream>

class MainAppSHM {
    int shm_fd = -1;
    size_t total_size = 0;
    std::string current_name;
    void* mapped_ptr = nullptr;

public:
    SharedBufferHeader* header = nullptr;
    uint8_t* pixel_data = nullptr;

    // Returns true if successfully created and mapped
    bool init(const std::string& name, uint32_t w, uint32_t h, uint32_t stride) {
        cleanup(); // Ensure we don't leak if called twice

        current_name = name;
        uint32_t pixel_size = stride * h;
        total_size = sizeof(SharedBufferHeader) + pixel_size;

        // 1. Create shared memory object 
        // Use O_EXCL to ensure we are creating a fresh buffer
        shm_fd = shm_open(current_name.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (shm_fd == -1) return false;

        // 2. Set the size of the shared memory segment
        if (ftruncate(shm_fd, total_size) == -1) {
            cleanup();
            return false;
        }

        // 3. Map into Main App memory space
        mapped_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (mapped_ptr == MAP_FAILED) {
            cleanup();
            return false;
        }

        mlock(mapped_ptr, total_size);

        // 4. Initialize Header
        header = (SharedBufferHeader*)mapped_ptr;
        header->width = w;
        header->height = h;
        header->stride = stride;
        header->data_size = pixel_size;
        header->frame_index = 0;
        header->lock_flag = 0;

        // 5. Point pixel_data to the memory immediately following the header
        pixel_data = (uint8_t*)mapped_ptr + sizeof(SharedBufferHeader);
        
        return true;
    }

    void write_frame(const uint8_t* src, double timestamp) {
        if (!header || !pixel_data) return;

        // Signal the Runner to wait
        header->lock_flag = 1;
        
        // Perform the copy
        // Note: We use the header's data_size which was set during init
        std::memcpy(pixel_data, src, header->data_size);
        
        header->capture_timestamp = timestamp;
        header->frame_index += 1;
        
        // Release the lock
        header->lock_flag = 0;
    }

    void cleanup() {
        if (mapped_ptr) {
            munlock(mapped_ptr, total_size);
            munmap(mapped_ptr, total_size);
            mapped_ptr = nullptr;
        }
        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
        }
        /*if (!current_name.empty()) {
            shm_unlink(current_name.c_str());
            current_name.clear();
        }*/
    }

    void remove_shm() {
        if (!current_name.empty()) {
            shm_unlink(current_name.c_str());
            current_name.clear();
        }
    }
    
    ~MainAppSHM() {
        cleanup();
    }
};

class RunnerSHM {
    int shm_fd = -1;
    size_t total_size = 0;
    void* mapped_ptr = nullptr;

public:
    SharedBufferHeader* header = nullptr;
    uint8_t* pixel_data = nullptr;

    // Now takes the name sent via IPC
    bool connect(const char* name) {
        // 1. Cleanup any existing mapping (crucial for resizes)
        disconnect();

        // 2. Open the shared memory object
        shm_fd = shm_open(name, O_RDWR, 0600);
        if (shm_fd == -1){
            return false;
        }

        // 3. Determine the size of the segment
        struct stat st;
        if (fstat(shm_fd, &st) == -1) {
            close(shm_fd);
            return false;
        }
        total_size = st.st_size;

        // 4. Map the memory
        mapped_ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (mapped_ptr == MAP_FAILED) {
            close(shm_fd);
            shm_fd = -1;
            return false;
        }

        mlock(mapped_ptr, total_size);

        // 5. Set up our pointers
        header = (SharedBufferHeader*)mapped_ptr;
        pixel_data = (uint8_t*)mapped_ptr + sizeof(SharedBufferHeader);
        
        return true;
    }

    void disconnect() {
        if (mapped_ptr) {
            munlock(mapped_ptr, total_size);
            munmap(mapped_ptr, total_size);
            mapped_ptr = nullptr;
            header = nullptr;
            pixel_data = nullptr;
        }
        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
        }
    }

    ~RunnerSHM() {
        disconnect();
    }
};