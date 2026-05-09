#pragma once
#ifndef IMG_UTILS_H
#define IMG_UTILS_H

#include <Accelerate/Accelerate.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>

extern "C" {
    typedef struct {
        uint8_t* data;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
    } PixelBuffer;

    // Returns a NEW PixelBuffer with its own allocated memory
    __attribute__((visibility("default")))
    PixelBuffer copy_buffer(PixelBuffer src);

    // Crops from a source to a NEW allocated PixelBuffer
    __attribute__((visibility("default")))
    PixelBuffer crop_buffer(PixelBuffer src, uint32_t x, uint32_t y, uint32_t cw, uint32_t ch);

    // Cleans up the data inside a PixelBuffer
    __attribute__((visibility("default")))
    void free_buffer(PixelBuffer pb);

    bool _save_frame_ppm(const uint8_t* data, size_t width, size_t height,
                         size_t bytes_per_row, const char* filename);

    bool _save_frame_image(const uint8_t* data, size_t width, size_t height,
                           size_t bytes_per_row, const char* filename);
}

#endif