#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <iostream>
#include <arm_neon.h>

struct BoundingRect {
    size_t x, y;
    size_t width, height;
    static inline bool has_point(const BoundingRect& rect, size_t px, size_t py) {
        return px >= rect.x && px < rect.x + rect.width &&
               py >= rect.y && py < rect.y + rect.height;
    }
};

struct ColorRGB {
    uint8_t r, g, b, a;
    ColorRGB(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255) : r(red), g(green), b(blue), a(alpha){}
};

struct ColorMatch {
    bool found;
    int x, y;
};

inline ColorMatch find_exact_color(
    const uint8_t* data,
    size_t frame_width,
    size_t frame_height,
    size_t bytes_per_row,
    BoundingRect rect,
    const ColorRGB& target_color,
    bool reverse = false,
    bool reverse_vertical = false
) {
    ColorMatch result = {false, 0, 0};
    uint32_t target_32 = (255 << 24) | (target_color.r << 16) | (target_color.g << 8) | target_color.b;
    uint32x4_t v_target = vdupq_n_u32(target_32);

    size_t end_y = rect.y + rect.height;
    size_t start_x = rect.x;
    size_t end_x = rect.x + rect.width;

    int y_start = reverse_vertical ? (int)(end_y - 1) : (int)rect.y;
    int y_end   = reverse_vertical ? (int)rect.y     : (int)end_y;
    int y_step  = reverse_vertical ? -1              : 1;

    for (int y = y_start; reverse_vertical ? y >= y_end : y < y_end; y += y_step) {
        const uint32_t* row_ptr = reinterpret_cast<const uint32_t*>(data + (y * bytes_per_row));
        
        if (!reverse) {
            size_t x = start_x;
            for (; x + 3 < end_x; x += 4) {
                uint32x4_t v_pixels = vld1q_u32(row_ptr + x);
                uint32x4_t v_cmp = vceqq_u32(v_pixels, v_target);
                if (vaddvq_u32(v_cmp) > 0) {
                    for (size_t i = 0; i < 4; i++) {
                        if (row_ptr[x + i] == target_32) return {true, (int)(x + i), y};
                    }
                }
            }
            for (; x < end_x; x++) {
                if (row_ptr[x] == target_32) return {true, (int)x, y};
            }
        } else {
            // Right-to-Left Search
            int x = (int)end_x - 1;
            // Process SIMD chunks (aligned to 4 pixels from the right)
            for (; x >= (int)start_x + 3; x -= 4) {
                // Load 4 pixels starting from (x-3) to get pixels [x-3, x-2, x-1, x]
                uint32x4_t v_pixels = vld1q_u32(row_ptr + (x - 3));
                uint32x4_t v_cmp = vceqq_u32(v_pixels, v_target);
                if (vaddvq_u32(v_cmp) > 0) {
                    // Check individual pixels from right to left
                    for (int i = 0; i < 4; i++) {
                        if (row_ptr[x - i] == target_32) return {true, x - i, y};
                    }
                }
            }
            // Cleanup remaining pixels
            for (; x >= (int)start_x; x--) {
                if (row_ptr[x] == target_32) return {true, x, y};
            }
        }
    }
    return result;
}

inline ColorMatch find_color_with_tolerance(
    const uint8_t* data,
    size_t frame_width,
    size_t frame_height,
    size_t bytes_per_row,
    BoundingRect rect,
    const ColorRGB& target_color,
    uint8_t tolerance,
    bool reverse = false,
    bool reverse_vertical = false
) {
    ColorMatch result = {false, 0, 0};
    uint8_t target_array[16] = {
        target_color.b, target_color.g, target_color.r, 0,
        target_color.b, target_color.g, target_color.r, 0,
        target_color.b, target_color.g, target_color.r, 0,
        target_color.b, target_color.g, target_color.r, 0
    };
    uint8x16_t v_target = vld1q_u8(target_array);
    uint8x16_t v_tolerance = vdupq_n_u8(tolerance);
    
    size_t end_y = rect.y + rect.height;
    size_t start_x = rect.x;
    size_t end_x = rect.x + rect.width;

    int y_start = reverse_vertical ? (int)(end_y - 1) : (int)rect.y;
    int y_end   = reverse_vertical ? (int)rect.y     : (int)end_y;
    int y_step  = reverse_vertical ? -1              : 1;

    for (int y = y_start; reverse_vertical ? y >= y_end : y < y_end; y += y_step) {
        const uint8_t* row_ptr = data + (y * bytes_per_row);
        
        if (!reverse) {
            size_t x = start_x;
            for (; x + 3 < end_x; x += 4) {
                uint8x16_t v_pixels = vld1q_u8(row_ptr + (x * 4));
                uint8x16_t v_diff = vabdq_u8(v_pixels, v_target);
                uint8x16_t v_cmp = vcleq_u8(v_diff, v_tolerance);
                uint32x4_t v_res = vreinterpretq_u32_u8(v_cmp);
                uint32x4_t v_temp = vandq_u32(v_res, vshrq_n_u32(v_res, 8));
                v_temp = vandq_u32(v_temp, vshrq_n_u32(v_res, 16));

                if (vmaxvq_u32(v_temp) & 0xFF) {
                    for (size_t i = 0; i < 4; i++) {
                        size_t offset = (x + i) * 4;
                        if (std::abs((int)row_ptr[offset] - target_color.b) <= tolerance &&
                            std::abs((int)row_ptr[offset+1] - target_color.g) <= tolerance &&
                            std::abs((int)row_ptr[offset+2] - target_color.r) <= tolerance) {
                            return {true, (int)(x + i), y};
                        }
                    }
                }
            }
            for (; x < end_x; x++) {
                size_t offset = x * 4;
                if (std::abs((int)row_ptr[offset] - target_color.b) <= tolerance &&
                    std::abs((int)row_ptr[offset+1] - target_color.g) <= tolerance &&
                    std::abs((int)row_ptr[offset+2] - target_color.r) <= tolerance) return {true, (int)x, y};
            }
        } else {
            int x = (int)end_x - 1;
            for (; x >= (int)start_x + 3; x -= 4) {
                // Load 4 pixels [x-3, x-2, x-1, x]
                uint8x16_t v_pixels = vld1q_u8(row_ptr + ((x - 3) * 4));
                uint8x16_t v_diff = vabdq_u8(v_pixels, v_target);
                uint8x16_t v_cmp = vcleq_u8(v_diff, v_tolerance);
                uint32x4_t v_res = vreinterpretq_u32_u8(v_cmp);
                uint32x4_t v_temp = vandq_u32(v_res, vshrq_n_u32(v_res, 8));
                v_temp = vandq_u32(v_temp, vshrq_n_u32(v_res, 16));

                if (vmaxvq_u32(v_temp) & 0xFF) {
                    for (int i = 0; i < 4; i++) {
                        size_t offset = (x - i) * 4;
                        if (std::abs((int)row_ptr[offset] - target_color.b) <= tolerance &&
                            std::abs((int)row_ptr[offset+1] - target_color.g) <= tolerance &&
                            std::abs((int)row_ptr[offset+2] - target_color.r) <= tolerance) {
                            return {true, x - i, y};
                        }
                    }
                }
            }
            for (; x >= (int)start_x; x--) {
                size_t offset = x * 4;
                if (std::abs((int)row_ptr[offset] - target_color.b) <= tolerance &&
                    std::abs((int)row_ptr[offset+1] - target_color.g) <= tolerance &&
                    std::abs((int)row_ptr[offset+2] - target_color.r) <= tolerance) return {true, x, y};
            }
        }
    }
    return result;
}

inline ColorMatch find_color_fast_sample(
    const uint8_t* data,
    size_t frame_width,
    size_t frame_height,
    size_t bytes_per_row,
    const BoundingRect& rect,
    const ColorRGB& target_color,
    uint8_t tolerance = 10,
    size_t skip_pixels = 4,
    size_t offset_x = 0,
    size_t offset_y = 0,
    bool reverse = false,
    bool reverse_vertical = false
) {
    ColorMatch result = {false, 0, 0};
    size_t end_y = rect.y + rect.height;
    size_t start_x = rect.x;
    size_t end_x = rect.x + rect.width;
    
    if (end_x > frame_width || end_y > frame_height) return result;

    // Calculate the first sampled Y from the bottom when reverse_vertical is true
    size_t last_y = rect.y + ((end_y - 1 - rect.y) / skip_pixels) * skip_pixels;

    int y_start = reverse_vertical ? (int)last_y    : (int)rect.y;
    int y_end   = reverse_vertical ? (int)rect.y   : (int)end_y;
    int y_step  = reverse_vertical ? -(int)skip_pixels : (int)skip_pixels;

    for (int y = y_start; reverse_vertical ? y >= y_end : y < y_end; y += y_step) {
        size_t row_start = (size_t)y * bytes_per_row;
        if (!reverse) {
            for (size_t x = start_x; x < end_x; x += skip_pixels) {
                size_t offset = row_start + x * 4;
                if (std::abs((int)data[offset+2] - (int)target_color.r) <= tolerance &&
                    std::abs((int)data[offset+1] - (int)target_color.g) <= tolerance &&
                    std::abs((int)data[offset]   - (int)target_color.b) <= tolerance) {
                    return {true, (int)(x + offset_x), (int)(y + offset_y)};
                }
            }
        } else {
            // Right-to-Left sampling
            // Calculate start position aligned to skip_pixels from the right
            size_t last_x = start_x + ((end_x - 1 - start_x) / skip_pixels) * skip_pixels;
            for (int x = (int)last_x; x >= (int)start_x; x -= (int)skip_pixels) {
                size_t offset = row_start + x * 4;
                if (std::abs((int)data[offset+2] - (int)target_color.r) <= tolerance &&
                    std::abs((int)data[offset+1] - (int)target_color.g) <= tolerance &&
                    std::abs((int)data[offset]   - (int)target_color.b) <= tolerance) {
                    return {true, x + (int)offset_x, (int)(y + offset_y)};
                }
            }
        }
    }
    return result;
}