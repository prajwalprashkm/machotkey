#include "opencv2/core.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
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
#include <stdexcept>
#include "ocr.h"
#include "img_utils.h"

extern RunnerSHM runner_shm;
extern RunnerState* g_runner_state;

extern "C" {
    typedef struct {
        void* mat_ptr;
        size_t width, height, stride, channels;
        bool readonly;
    } OpenCVMat;

    typedef struct {
        int x;
        int y;
        double score;
    } OpenCVTemplateMatch;

    typedef struct {
        OpenCVTemplateMatch* data;
        size_t size;
    } OpenCVTemplateMatchVector;

    OpenCVMat _OpenCVMat_bgra_to_bgr(OpenCVMat mat){
        auto src = (cv::Mat*) mat.mat_ptr;

        if(src->type() != CV_8UC4){
            throw std::runtime_error("Cannot convert to BGR; input not in BGRA!");
        }

        cv::Mat* dst = new cv::Mat();
        cv::cvtColor(*(cv::Mat*) mat.mat_ptr, *dst, cv::COLOR_BGRA2BGR);
        
        OpenCVMat ret;
        ret.mat_ptr = (void*) dst;
        ret.channels = 3;
        ret.width = mat.width;
        ret.height = mat.height;
        ret.stride = mat.stride;
        ret.readonly = false;
        
        return ret;
    }

    OpenCVMat _OpenCVMat_bgr_to_bgra(OpenCVMat mat){
        auto src = (cv::Mat*) mat.mat_ptr;

        if(src->type() != CV_8UC3){
            throw std::runtime_error("Cannot convert to BGRA; input not in BGR!");
        }

        cv::Mat* dst = new cv::Mat();
        cv::cvtColor(*(cv::Mat*) mat.mat_ptr, *dst, cv::COLOR_BGR2BGRA);
        
        OpenCVMat ret;
        ret.mat_ptr = (void*) dst;
        ret.channels = 4;
        ret.width = mat.width;
        ret.height = mat.height;
        ret.stride = mat.stride;
        ret.readonly = false;
        
        return ret;
    }

    OpenCVMat _PixelBuffer_to_OpenCVMat(bool readonly, PixelBuffer buffer) {
        OpenCVMat ret;

        if (readonly) {
            ret.mat_ptr = (void*) new cv::Mat(buffer.height, buffer.width, CV_8UC4, buffer.data, buffer.stride);
            ret.readonly = true;
        } else {
            cv::Mat* dst = new cv::Mat();
            
            cv::Mat src_view(buffer.height, buffer.width, CV_8UC4, buffer.data, buffer.stride);
            
            src_view.copyTo(*dst);
            
            ret.mat_ptr = (void*)dst;
            ret.readonly = false;
        }

        cv::Mat* m = (cv::Mat*)ret.mat_ptr;
        ret.width = m->cols;
        ret.height = m->rows;
        ret.stride = m->step[0];
        ret.channels = 4; 
        return ret;
    }

    PixelBuffer _OpenCVMat_to_PixelBuffer(OpenCVMat mat){
        if(mat.channels != 4){
            if(mat.channels == 3){
                mat = _OpenCVMat_bgr_to_bgra(mat);
            }else{
                throw std::runtime_error("Unkown mat type! Please use BGR or BGRA.");
            }
        }

        PixelBuffer ret;
        auto src = (cv::Mat*) mat.mat_ptr;
        auto size = src->total()*src->elemSize();

        ret.data = (uint8_t*) malloc(size);
        memcpy(ret.data, src->data, size);
        ret.width = mat.width;
        ret.height = mat.height;
        ret.stride = src->step;

        return ret;
    }

    OpenCVMat _OpenCVMat_resize(OpenCVMat mat, size_t w, size_t h){
        auto src = (cv::Mat*) mat.mat_ptr;
        auto dst = new cv::Mat();

        int interpolation = (w < src->cols) ? cv::INTER_AREA : cv::INTER_LINEAR;

        cv::resize(*src, *dst, cv::Size(w, h), 0, 0, interpolation);
        return { (void*) dst, w, h, mat.stride, false };
    }

    OpenCVTemplateMatch _OpenCVTemplateMatch_refine_match(cv::Mat* scene, cv::Mat* tpl, cv::Point coarseLoc, double threshold, cv::Mat* mask = nullptr) {
        OpenCVTemplateMatch refined = {-1, -1, 0.0};

        // 1. Calculate original scale coordinates (pyrDown is exactly 2x)
        int centerX = coarseLoc.x * 2;
        int centerY = coarseLoc.y * 2;

        // 2. Define a small search neighborhood (Template size + small buffer)
        int buffer = 6; 
        int roiX = std::max(0, centerX - buffer);
        int roiY = std::max(0, centerY - buffer);
        int roiW = std::min(tpl->cols + (buffer * 2), scene->cols - roiX);
        int roiH = std::min(tpl->rows + (buffer * 2), scene->rows - roiY);

        // 3. Match in high-res
        cv::Mat roiMat = (*scene)(cv::Rect(roiX, roiY, roiW, roiH));
        cv::Mat res;
        if(mask == nullptr){
            cv::matchTemplate(roiMat, *tpl, res, cv::TM_CCOEFF_NORMED);
        }else{
            cv::matchTemplate(roiMat, *tpl, res, cv::TM_CCOEFF_NORMED, *mask);
        }

        double maxVal;
        cv::Point maxLoc;


        cv::patchNaNs(res, -1.0); 
        cv::minMaxLoc(res, nullptr, &maxVal, nullptr, &maxLoc);

        if (maxVal >= threshold) {
            refined.x = maxLoc.x + roiX;
            refined.y = maxLoc.y + roiY;
            refined.score = maxVal;
        }

        return refined;
    }

    static OpenCVMat emptyOpenCVMat = {.channels = 0};

    OpenCVTemplateMatchVector _OpenCVMat_match_template(OpenCVMat scene_m, OpenCVMat tpl_m, double threshold, OpenCVMat mask) {
        OpenCVTemplateMatchVector result = {nullptr, 0};

        cv::Mat* scene = static_cast<cv::Mat*>(scene_m.mat_ptr);
        cv::Mat* tpl   = static_cast<cv::Mat*>(tpl_m.mat_ptr);

        // 1. Safety Check
        if (tpl->cols > scene->cols || tpl->rows > scene->rows) return result;

        // 2. Perform the full-resolution match
        cv::Mat result_map;
        try {
            if(mask.channels == 0){
                cv::matchTemplate(*scene, *tpl, result_map, cv::TM_CCOEFF_NORMED);
            }else{
                cv::matchTemplate(*scene, *tpl, result_map, cv::TM_CCOEFF_NORMED, *((cv::Mat*)mask.mat_ptr));
            }
        } catch (...) {
            return result;
        }

        cv::patchNaNs(result_map, -1.0); 
        if (threshold == -1) {
            double maxVal;
            cv::Point maxLoc;
            cv::minMaxLoc(result_map, nullptr, &maxVal, nullptr, &maxLoc);

            result.size = 1;
            result.data = (OpenCVTemplateMatch*) malloc(sizeof(OpenCVTemplateMatch));
            result.data[0] = {maxLoc.x, maxLoc.y, maxVal};

            return result;
        } else {
            size_t capacity = 16;
            result.data = (OpenCVTemplateMatch*) malloc(capacity * sizeof(OpenCVTemplateMatch));
            result.size = 0;

            while (true) {
                double maxVal;
                cv::Point maxLoc;
                cv::minMaxLoc(result_map, nullptr, &maxVal, nullptr, &maxLoc);

                if (maxVal < threshold) break;

                if (result.size >= capacity) {
                    capacity *= 2;
                    result.data = (OpenCVTemplateMatch*) realloc(result.data, capacity * sizeof(OpenCVTemplateMatch));
                }
                result.data[result.size] = {maxLoc.x, maxLoc.y, maxVal};
                result.size++;

                int mask_w = tpl->cols;
                int mask_h = tpl->rows;

                int start_x = std::max(0, maxLoc.x - mask_w / 2);
                int start_y = std::max(0, maxLoc.y - mask_h / 2);
                
                cv::Rect mask(start_x, start_y, mask_w, mask_h);
                
                mask &= cv::Rect(0, 0, result_map.cols, result_map.rows);

                cv::rectangle(result_map, mask, cv::Scalar(-1.0), -1);
            }
            result.data = (OpenCVTemplateMatch*) realloc(result.data, result.size);
        }

        return result;
    }

    OpenCVTemplateMatchVector _OpenCVMat_match_template_pyr(OpenCVMat scene_m, OpenCVMat tpl_m, double threshold, OpenCVMat mask_m, OpenCVMat scene_pyr, OpenCVMat tpl_pyr, OpenCVMat mask_pyr) {
        OpenCVTemplateMatchVector result = {nullptr, 0};

        cv::Mat* scene = static_cast<cv::Mat*>(scene_m.mat_ptr);
        cv::Mat* tpl   = static_cast<cv::Mat*>(tpl_m.mat_ptr);
        cv::Mat* mask  = mask_m.channels != 0 ? static_cast<cv::Mat*>(mask_m.mat_ptr) : nullptr;

        // 1. Safety Check
        if (tpl->cols > scene->cols || tpl->rows > scene->rows) return result;

        // 2. Downscale both images (1/2 width and height)
        // We use pyrDown because it applies a Gaussian blur before shrinking, 
        // which prevents "aliasing" artifacts that ruin matching scores.
        cv::Mat scene_small, tpl_small, mask_small;
        if(scene_pyr.channels == 0){
            cv::pyrDown(*scene, scene_small);
        }else{
            scene_small = *((cv::Mat*) scene_pyr.mat_ptr);
        }
        if(tpl_pyr.channels == 0){
            cv::pyrDown(*tpl, tpl_small);
        }else{
            tpl_small = *((cv::Mat*) tpl_pyr.mat_ptr);
        }
        if(mask_m.channels != 0 && mask_pyr.channels == 0){
            cv::pyrDown(*mask, mask_small);
        }else if(mask_m.channels != 0){
            scene_small = *((cv::Mat*) mask_pyr.mat_ptr);
        }

        // Check if template is still smaller than scene after downscaling
        if (tpl_small.cols > scene_small.cols || tpl_small.rows > scene_small.rows) {
            // If template is too small to downscale, fall back to a normal match
            return _OpenCVMat_match_template(scene_m, tpl_m, threshold, mask_m); 
        }

        // 3. Coarse Match (Fast)
        cv::Mat result_map_small;
        
        if(mask_m.channels == 0){
            cv::matchTemplate(scene_small, tpl_small, result_map_small, cv::TM_CCOEFF_NORMED);
        }else{
            cv::matchTemplate(scene_small, tpl_small, result_map_small, cv::TM_CCOEFF_NORMED, mask_small);
        }

        double maxVal;
        cv::Point maxLoc;
        cv::minMaxLoc(result_map_small, nullptr, &maxVal, nullptr, &maxLoc);

        // 4. Define the Refinement ROI in the Original Image
        // The match at 1/2 scale means our target is at roughly (maxLoc.x * 2, maxLoc.y * 2)
        int search_x = maxLoc.x * 2;
        int search_y = maxLoc.y * 2;

        // We add a small padding (e.g., 4 pixels) to account for rounding errors
        int padding = 4;
        int roi_x = std::max(0, search_x - padding);
        int roi_y = std::max(0, search_y - padding);
        
        // Ensure ROI doesn't go out of bounds
        int roi_w = std::min(tpl->cols + (padding * 2), scene->cols - roi_x);
        int roi_h = std::min(tpl->rows + (padding * 2), scene->rows - roi_y);

        cv::Rect roi(roi_x, roi_y, roi_w, roi_h);
        cv::Mat scene_roi = (*scene)(roi);

        // 5. Fine Match (Precise)
        cv::Mat result_map_fine;
        try {
            if(mask_m.channels == 0){
                cv::matchTemplate(scene_roi, *tpl, result_map_fine, cv::TM_SQDIFF);
            }else{
                cv::matchTemplate(scene_roi, *tpl, result_map_fine, cv::TM_CCOEFF_NORMED, *mask);
            }
        } catch (...) {
            return result;
        }

        if(threshold == -1){
            double finalMaxVal;
            cv::Point finalMaxLoc;
            cv::minMaxLoc(result_map_fine, nullptr, &finalMaxVal, nullptr, &finalMaxLoc);

            result.size = 1;
            result.data = (OpenCVTemplateMatch*) malloc(sizeof(OpenCVTemplateMatch));

            *result.data = {finalMaxLoc.x+roi_x, finalMaxLoc.y+roi_y, maxVal};

            return result;
        }else{
            result.data = (OpenCVTemplateMatch*) malloc(16 * sizeof(OpenCVTemplateMatch));
            result.size = 0;
            size_t capacity = 16;

            while(true){
                double maxVal;
                cv::Point maxLoc;
                
                cv::patchNaNs(result_map_small, -1.0); 
                cv::minMaxLoc(result_map_small, nullptr, &maxVal, nullptr, &maxLoc);

                if (maxVal < threshold) {
                    break;
                }

                int mask_w = tpl_small.cols;
                int mask_h = tpl_small.rows;
                
                int start_x = std::max(0, maxLoc.x - mask_w / 2);
                int start_y = std::max(0, maxLoc.y - mask_h / 2);
                int end_x = std::min(result_map_small.cols, start_x + mask_w);
                int end_y = std::min(result_map_small.rows, start_y + mask_h);

                cv::rectangle(result_map_small, 
                            cv::Point(start_x, start_y), 
                            cv::Point(end_x, end_y), 
                            cv::Scalar(-1.0), 
                            -1); 

                OpenCVTemplateMatch refined = _OpenCVTemplateMatch_refine_match(scene, tpl, maxLoc, threshold, mask);
                
                if (refined.score >= threshold) {
                    result.size += 1;
                    if(result.size > capacity){
                        capacity *= 2;
                        result.data = (OpenCVTemplateMatch*) realloc(result.data, capacity*sizeof(OpenCVTemplateMatch));
                    }
                    result.data[result.size - 1] = refined;
                }
            }
            result.data = (OpenCVTemplateMatch*) realloc(result.data, result.size*sizeof(OpenCVTemplateMatch));
            return result;
        }
    }
    OpenCVMat _OpenCVMat_downsample(OpenCVMat mat){
        OpenCVMat ret;
        ret.mat_ptr = new cv::Mat();
        cv::pyrDown(*((cv::Mat*) mat.mat_ptr), *((cv::Mat*) ret.mat_ptr));

        auto ret_mat = *((cv::Mat*) ret.mat_ptr);

        ret.channels = ret_mat.channels();
        ret.width = ret_mat.cols;
        ret.height = ret_mat.rows;
        ret.stride = ret_mat.step;
        ret.readonly = false;
        return ret;
    }
    OpenCVMat _OpenCVMat_upsample(OpenCVMat mat){
        OpenCVMat ret;
        ret.mat_ptr = new cv::Mat();
        cv::pyrUp(*((cv::Mat*) mat.mat_ptr), *((cv::Mat*) ret.mat_ptr));

        auto ret_mat = *((cv::Mat*) ret.mat_ptr);

        ret.channels = ret_mat.channels();
        ret.width = ret_mat.cols;
        ret.height = ret_mat.rows;
        ret.stride = ret_mat.step;
        ret.readonly = false;
        return ret;
    }
    OpenCVMat _OpenCVMat_extract_channel(OpenCVMat mat, size_t channel){
        OpenCVMat ret;
        ret.mat_ptr = new cv::Mat();

        cv::extractChannel(*((cv::Mat*) mat.mat_ptr), *((cv::Mat*) ret.mat_ptr), channel);
        auto ret_mat = *((cv::Mat*) ret.mat_ptr);
        ret.width = ret_mat.cols;
        ret.height = ret_mat.rows;
        ret.channels = ret_mat.channels();
        ret.stride = ret_mat.step;
        ret.readonly = false;
        return ret;
    }
    OpenCVMat _OpenCVMat_load_from_file(const char* filename){
        OpenCVMat ret; 
        auto mat = cv::imread(filename, cv::IMREAD_UNCHANGED);

        cv::Mat bgra;
        if(mat.channels() == 1){
            cv::cvtColor(mat, bgra, cv::COLOR_GRAY2BGRA);
        }else if(mat.channels() == 3){
            cv::cvtColor(mat, bgra, cv::COLOR_BGR2BGRA);
        }else{
            bgra = mat;
        }

        ret.mat_ptr = (void*) new cv::Mat(bgra);
        ret.channels = bgra.channels();
        ret.width = bgra.cols;
        ret.height = bgra.rows;
        ret.stride = bgra.step;
        ret.readonly = false;

        return ret;
    }

    void _OpenCVMat_free(OpenCVMat mat){
        if(mat.mat_ptr != nullptr){
            delete (cv::Mat*) mat.mat_ptr;
            mat.mat_ptr = nullptr;
        }
    }
    void _OpenCVTemplateMatchVector_free(OpenCVTemplateMatchVector vec){
        if(vec.data != nullptr){
            free(vec.data);
            vec.size = 0;
            vec.data = nullptr;
        }
    }
}

#define THROW_LUA_ERROR(s, msg) throw sol::error(get_current_location(s)+msg)

class OpenCVBridge {
public:
    static void register_code(sol::state& lua, sol::table& system) {
        // Create the mouse table
        sol::table opencv = lua.create_table();
        


        system["opencv"] = opencv;
    }
};
