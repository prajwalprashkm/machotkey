/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

//
//  app_manager.h
//  Manages both webview and overlay in harmony
//

#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "webview.h"
#include <functional>

class AppManager {
public:
    AppManager();
    ~AppManager();
    
    // Initialize both systems
    void init();
    
    // Create the webview window
    WebViewWindow* create_webview(const std::string& title, int x, int y, int width, int height, bool resizable, bool frameless);
    
    // Bind C++ functions to JavaScript
    void bind(const std::string& name, std::function<std::string(const std::string&)> fn);
    
    // Set the overlay render callback
    void set_overlay_render(std::function<void()> callback);
    
    // Show/hide overlay
    void set_overlay_visible(bool visible);
    
    // Run the unified event loop (call this instead of webview's run_blocking)
    void run();
    
private:
    WebViewApp* webview_app;
    std::function<void()> overlay_render_fn;
    bool overlay_visible;
};
#endif /* APP_MANAGER_H */