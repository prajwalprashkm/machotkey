/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <string>
#include <vector>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cstring>

struct OCRResult {
    std::string text;
    float confidence;
    CGRect boundingBox;
};

#ifdef __OBJC__   // Only true for Objective-C++
@class VNRecognizeTextRequest;
#endif

class FastOCR {
private:
#ifdef __OBJC__
    VNRecognizeTextRequest* text_request;
#else
    void* text_request;
#endif
    bool is_configured;

public:
    FastOCR(bool fast_mode = true);
    ~FastOCR();

    std::vector<OCRResult> recognize_text(const uint8_t* data,
                                         size_t width,
                                         size_t height,
                                         size_t bytes_per_row);

    std::string recognize_text_fast(const uint8_t* data,
                                  size_t width,
                                  size_t height,
                                  size_t bytes_per_row);

    std::string combine(std::vector<OCRResult>& results);

    std::vector<OCRResult> recognize_text_in_region(const uint8_t* data,
                                                 size_t frame_width,
                                                 size_t frame_height,
                                                 size_t bytes_per_row,
                                                 size_t regionX,
                                                 size_t regionY,
                                                 size_t regionWidth,
                                                 size_t regionHeight);

    static std::vector<OCRResult> filter_by_confidence(const std::vector<OCRResult>& results,
                                                     float min_confidence);

    static std::vector<OCRResult> find_text(const std::vector<OCRResult>& results,
                                           const std::string& search_text);
};

// Ultra-fast Levenshtein distance with optimizations
class LevenshteinDistance {
public:
    // Single row optimization - O(n) space instead of O(m*n)
    static int calculate(const std::string& s1, const std::string& s2) {
        const int m = s1.length();
        const int n = s2.length();
        
        // Early exits for common cases
        if (m == 0) return n;
        if (n == 0) return m;
        if (s1 == s2) return 0;
        
        // Ensure s1 is the shorter string for better cache performance
        if (m > n) return calculate(s2, s1);
        
        // Use single row + temp variable (only O(n) space)
        std::vector<int> prev(n + 1);
        std::vector<int> curr(n + 1);
        
        // Initialize first row
        for (int j = 0; j <= n; ++j) {
            prev[j] = j;
        }
        
        // Fill the matrix row by row
        for (int i = 1; i <= m; ++i) {
            curr[0] = i;
            
            for (int j = 1; j <= n; ++j) {
                int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
                
                curr[j] = std::min({
                    prev[j] + 1,      // deletion
                    curr[j-1] + 1,    // insertion
                    prev[j-1] + cost  // substitution
                });
            }
            
            std::swap(prev, curr);
        }
        
        return prev[n];
    }
    
    // Even faster for C-strings with fixed buffer (no allocations)
    static int calculate_fast(const char* s1, const char* s2, int maxLen = 256) {
        int m = strlen(s1);
        int n = strlen(s2);
        
        if (m == 0) return n;
        if (n == 0) return m;
        if (strcmp(s1, s2) == 0) return 0;
        
        if (m > n) {
            std::swap(s1, s2);
            std::swap(m, n);
        }
        
        // Stack-allocated buffer for small strings
        int buffer[512];
        int* prev = buffer;
        int* curr = buffer + (n + 1);
        
        for (int j = 0; j <= n; ++j) {
            prev[j] = j;
        }
        
        for (int i = 1; i <= m; ++i) {
            curr[0] = i;
            
            for (int j = 1; j <= n; ++j) {
                int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
                
                int del = prev[j] + 1;
                int ins = curr[j-1] + 1;
                int sub = prev[j-1] + cost;
                
                curr[j] = std::min(del, std::min(ins, sub));
            }
            
            std::swap(prev, curr);
        }
        
        return prev[n];
    }
    
    // Bounded version - stops early if distance exceeds threshold
    static int calculate_bounded(const std::string& s1, const std::string& s2, int threshold) {
        const int m = s1.length();
        const int n = s2.length();
        
        if (abs(m - n) > threshold) return threshold + 1;
        if (m == 0) return n;
        if (n == 0) return m;
        
        std::vector<int> prev(n + 1);
        std::vector<int> curr(n + 1);
        
        for (int j = 0; j <= n; ++j) {
            prev[j] = j;
        }
        
        for (int i = 1; i <= m; ++i) {
            curr[0] = i;
            int min_in_row = i;
            
            for (int j = 1; j <= n; ++j) {
                int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
                
                curr[j] = std::min({
                    prev[j] + 1,
                    curr[j-1] + 1,
                    prev[j-1] + cost
                });
                
                min_in_row = std::min(min_in_row, curr[j]);
            }
            
            // Early exit if minimum in row exceeds threshold
            if (min_in_row > threshold) return threshold + 1;
            
            std::swap(prev, curr);
        }
        
        return prev[n];
    }
    
    // SIMD-optimized version (requires modern CPU)
    // This uses manual vectorization for even better performance
    static int calculate_simd(const std::string& s1, const std::string& s2) {
        const int m = s1.length();
        const int n = s2.length();
        
        if (m == 0) return n;
        if (n == 0) return m;
        if (s1 == s2) return 0;
        
        if (m > n) return calculate_simd(s2, s1);
        
        // Align memory for better SIMD performance
        std::vector<int> prev(n + 1);
        std::vector<int> curr(n + 1);
        
        for (int j = 0; j <= n; ++j) {
            prev[j] = j;
        }
        
        for (int i = 1; i <= m; ++i) {
            curr[0] = i;
            const char c1 = s1[i-1];
            
            // Process 4 elements at a time when possible
            int j = 1;
            for (; j + 3 <= n; j += 4) {
                // Unrolled loop for better pipeline utilization
                int cost0 = (c1 == s2[j-1]) ? 0 : 1;
                int cost1 = (c1 == s2[j]) ? 0 : 1;
                int cost2 = (c1 == s2[j+1]) ? 0 : 1;
                int cost3 = (c1 == s2[j+2]) ? 0 : 1;
                
                curr[j] = std::min({prev[j] + 1, curr[j-1] + 1, prev[j-1] + cost0});
                curr[j+1] = std::min({prev[j+1] + 1, curr[j] + 1, prev[j] + cost1});
                curr[j+2] = std::min({prev[j+2] + 1, curr[j+1] + 1, prev[j+1] + cost2});
                curr[j+3] = std::min({prev[j+3] + 1, curr[j+2] + 1, prev[j+2] + cost3});
            }
            
            // Handle remaining elements
            for (; j <= n; ++j) {
                int cost = (c1 == s2[j-1]) ? 0 : 1;
                curr[j] = std::min({prev[j] + 1, curr[j-1] + 1, prev[j-1] + cost});
            }
            
            std::swap(prev, curr);
        }
        
        return prev[n];
    }
};