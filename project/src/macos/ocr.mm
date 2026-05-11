/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#include "../include/ocr.h"
#import <Vision/Vision.h>
#import <CoreImage/CoreImage.h>
#import <CoreGraphics/CoreGraphics.h>
#include <string>
#include <vector>
#include <iostream>
#import <CoreVideo/CVPixelBuffer.h>
#include <algorithm>
#include "utils.h"

FastOCR::FastOCR(bool fast_mode) : text_request(nil), is_configured(false) {
    @autoreleasepool {
        text_request = [[VNRecognizeTextRequest alloc] init];

        if (@available(macOS 13.0, *)) {
            // macOS 13+ supports fast recognition level
            if (fast_mode) {
                text_request.recognitionLevel = VNRequestTextRecognitionLevelFast;
            } else {
                text_request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            }

            // Disable automatic language detection for speed
            text_request.automaticallyDetectsLanguage = NO;
            text_request.usesLanguageCorrection = NO;

            // Manually specify language(s) for better performance
            text_request.recognitionLanguages = @[@"en-US"]; // English only

            // Use latest revision for best performance
            text_request.revision = VNRecognizeTextRequestRevision3;
        } else {
            // macOS 11–12
            text_request.recognitionLevel = VNRequestTextRecognitionLevelFast;
        }

        // Optimize for speed
        text_request.usesLanguageCorrection = NO;

        is_configured = true;
    }
}

FastOCR::~FastOCR() {
    if (text_request) {
        text_request = nil;
    }
}

std::vector<OCRResult> FastOCR::recognize_text(const uint8_t* data, 
                                              size_t width, 
                                              size_t height, 
                                              size_t bytes_per_row) {
    
    
    // Call the Region version with full-screen bounds
    return recognize_text_in_region(data, width, height, bytes_per_row, 0, 0, width, height);
}

std::vector<OCRResult> FastOCR::recognize_text_in_region(const uint8_t* data,
                                                      size_t frame_width,
                                                      size_t frame_height,
                                                      size_t bytes_per_row,
                                                      size_t regionX,
                                                      size_t regionY,
                                                      size_t regionWidth,
                                                      size_t regionHeight) {
    std::vector<OCRResult> results;
    if (!is_configured || !data || regionWidth == 0 || regionHeight == 0) return results;

    @autoreleasepool {
        // 1. STABILITY FIX: Create a local copy of the pixel data
        // This prevents the Main App from mutating the buffer while Vision is scanning it.
        size_t bufferSize = bytes_per_row * frame_height;
        void* localCopy = malloc(bufferSize);
        if (!localCopy) return results;
        memcpy(localCopy, data, bufferSize);

        // 2. Normalized ROI
        double normX = std::clamp((double)regionX / frame_width, 0.0, 1.0);
        double normY = std::clamp((double)(frame_height - regionY - regionHeight) / frame_height, 0.0, 1.0);
        double normW = std::clamp((double)regionWidth / frame_width, 1.0e-5, 1.0 - normX);
        double normH = std::clamp((double)regionHeight / frame_height, 1.0e-5, 1.0 - normY);
        [text_request setRegionOfInterest:CGRectMake(normX, normY, normW, normH)];

        // 3. Create CVPixelBuffer with a Release Callback to free our malloc
        CVPixelBufferRef pixelBuffer = NULL;
        OSStatus status = CVPixelBufferCreateWithBytes(
            kCFAllocatorDefault,
            frame_width,
            frame_height,
            kCVPixelFormatType_32BGRA, 
            localCopy,
            bytes_per_row,
            [](void *releaseRefCon, const void *baseAddress) {
                free((void*)baseAddress); // Free the malloc'd copy when Vision is done
            },
            NULL, NULL,
            &pixelBuffer
        );

        if (status != kCVReturnSuccess || !pixelBuffer) {
            free(localCopy);
            return results;
        }

        VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] 
            initWithCVPixelBuffer:pixelBuffer 
            options:@{}];

        NSError* error = nil;
        // Use Revision 2 if Revision 3 continues to be unstable with your setup
        // text_request.revision = VNRecognizeTextRequestRevision2;

        if ([handler performRequests:@[text_request] error:&error]) {
            NSArray<VNRecognizedTextObservation*>* observations = text_request.results;
            if (observations) {
                for (VNRecognizedTextObservation* obs in observations) {
                    NSArray<VNRecognizedText*>* candidates = [obs topCandidates:1];
                    if (candidates && [candidates count] > 0) {
                        VNRecognizedText* top = [candidates firstObject];
                        if (top) {
                            OCRResult r;
                            r.text = [top.string UTF8String];
                            r.confidence = top.confidence;
                            CGRect box = obs.boundingBox;
                            r.boundingBox.origin.x = (box.origin.x * regionWidth) + regionX;
                            r.boundingBox.origin.y = ((1.0 - box.origin.y - box.size.height) * regionHeight) + regionY;
                            r.boundingBox.size.width = box.size.width * regionWidth;
                            r.boundingBox.size.height = box.size.height * regionHeight;
                            results.push_back(r);
                        }
                    }
                }
            }
        }else{
            std::cout << "vision error: " << error.userInfo << " (" << error.code << ")" << std::endl;
        }

        CVPixelBufferRelease(pixelBuffer);
    }
    return results;
}

std::string FastOCR::recognize_text_fast(const uint8_t* data,
                                       size_t width,
                                       size_t height,
                                       size_t bytes_per_row) {
    auto results = recognize_text(data, width, height, bytes_per_row);
    std::string combined;
    for (const auto& result : results) {
        if (!combined.empty()) combined += " ";
        combined += result.text;
    }
    return combined;
}

std::string FastOCR::combine(std::vector<OCRResult>& results){
    std::string combined;
    for (const auto& result : results) {
        if (!combined.empty()) combined += " ";
        combined += result.text;
    }
    return combined;
}




std::vector<OCRResult> FastOCR::filter_by_confidence(
    const std::vector<OCRResult>& results, float min_confidence) {
    std::vector<OCRResult> filtered;
    for (const auto& result : results) {
        if (result.confidence >= min_confidence) {
            filtered.push_back(result);
        }
    }
    return filtered;
}

std::vector<OCRResult> FastOCR::find_text(
    const std::vector<OCRResult>& results, const std::string& search_text) {
    std::vector<OCRResult> found;
    for (const auto& result : results) {
        if (result.text.find(search_text) != std::string::npos) {
            found.push_back(result);
        }
    }
    return found;
}
