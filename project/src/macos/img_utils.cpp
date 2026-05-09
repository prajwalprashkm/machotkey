#include "img_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

extern "C" {
    PixelBuffer copy_buffer(PixelBuffer src) {
        if (!src.data) return { nullptr, 0, 0, 0 };

        size_t size = src.stride * src.height;
        uint8_t* new_data = (uint8_t*)malloc(size);
        if (new_data) {
            memcpy(new_data, src.data, size);
        }

        return { new_data, src.width, src.height, src.stride };
    }

    PixelBuffer crop_buffer(PixelBuffer src, uint32_t x, uint32_t y, uint32_t cw, uint32_t ch) {
        if (!src.data || x >= src.width || y >= src.height) return { nullptr, 0, 0, 0 };

        uint32_t actual_w = (x + cw > src.width) ? (src.width - x) : cw;
        uint32_t actual_h = (y + ch > src.height) ? (src.height - y) : ch;

        uint32_t dest_stride = actual_w * 4;
        uint8_t* dest_data = (uint8_t*)malloc(dest_stride * actual_h);

        vImage_Buffer v_src = { src.data + (y * src.stride) + (x * 4), actual_h, actual_w, src.stride };
        vImage_Buffer v_dest = { dest_data, actual_h, actual_w, dest_stride };
        
        vImageCopyBuffer(&v_src, &v_dest, 4, kvImageNoFlags);

        return { dest_data, actual_w, actual_h, dest_stride };
    }

    void free_buffer(PixelBuffer pb) {
        if (pb.data) {
            free(pb.data);
        }
    }
    static std::string lowercase_extension(const char* filename) {
        if (!filename) return {};
        std::filesystem::path path(filename);
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return ext;
    }

    static bool is_supported_image_extension(const std::string& ext) {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
               ext == ".tiff" || ext == ".tif" || ext == ".webp" || ext == ".ppm";
    }

    bool _save_frame_ppm(const uint8_t* data, size_t width, size_t height,
                         size_t bytes_per_row, const char* filename) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            return false;
        }

        file << "P6\n" << width << " " << height << "\n255\n";

        for (size_t y = 0; y < height; y++) {
            for (size_t x = 0; x < width; x++) {
                size_t offset = y * bytes_per_row + x * 4;
                uint8_t r = data[offset + 2];
                uint8_t g = data[offset + 1];
                uint8_t b = data[offset + 0];

                file.put(r);
                file.put(g);
                file.put(b);
            }
        }

        file.close();
        return true;
    }

    bool _save_frame_image(const uint8_t* data, size_t width, size_t height,
                           size_t bytes_per_row, const char* filename) {
        if (!data || !filename || width == 0 || height == 0 || bytes_per_row < width * 4) {
            return false;
        }

        const std::string ext = lowercase_extension(filename);
        if (!is_supported_image_extension(ext)) {
            return false;
        }

        if (ext == ".ppm") {
            return _save_frame_ppm(data, width, height, bytes_per_row, filename);
        }

        try {
            const cv::Mat bgra_view(static_cast<int>(height), static_cast<int>(width), CV_8UC4,
                                    const_cast<uint8_t*>(data), bytes_per_row);
            cv::Mat bgr;
            cv::cvtColor(bgra_view, bgr, cv::COLOR_BGRA2BGR);
            return cv::imwrite(filename, bgr);
        } catch (...) {
            return false;
        }
    }
}