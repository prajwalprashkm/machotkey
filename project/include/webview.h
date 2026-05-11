/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <atomic>
#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <thread>

enum class AppMenuAction {
    ToggleHeadless,
    LoadProject,
    RunProjectMacro,
    RunQuickScript,
    StopMacro,
    PauseResumeMacro
};

class WebViewAppImpl;

class WebViewWindow {
public:
    WebViewWindow(WebViewAppImpl* parent, const std::string& title, int x, int y, 
                  int width, int height, bool resizable = true, bool frameless = false);
    ~WebViewWindow();
    void set_html(const std::string& html_content);
    void set_html_resource(const std::string& resource_name, const std::string& subdirectory);
    void load_project_html_file(const std::string& absolute_html_path, const std::string& absolute_project_dir);
    void load_url(const std::string& url);
    void set_macro_ui_handler(std::function<void(const std::string&, const std::string&)> handler);
    void close();
    void send_to_js(const std::string& js);
    void set_title(const std::string& title);
    void center();
    void set_ignores_mouse(bool ignores);
    void set_size(int width, int height);
    void set_position(int x, int y);
    void get_position_async(std::function<void(int, int)> callback);
    void minimize();
    void maximize();
    void exit_fullscreen();
    void show();
    void hide();
    void set_opacity(float opacity);
    void bring_to_front(bool key);
    uint32_t get_native_window_number() const;
    bool is_ready();

    int x = -1, y = -1, w = -1, h = -1;
private:
    void* impl;
};

class WebViewApp {
public:
    WebViewApp();
    ~WebViewApp();
    
    WebViewWindow* create_window(const std::string& title, int x, int y, 
                                 int width, int height, bool resizable = true, 
                                 bool frameless = false);
    void destroy_window(WebViewWindow* window);
    void bind(const std::string& name, std::function<std::string(const std::string&)> fn);
    void run();  // Non-blocking - processes events
    void stop();
    void quit();
    void run_blocking();  // Blocking - for simple apps
    void set_accessory(bool isAccessory);
    void set_menu_action_handler(std::function<void(AppMenuAction)> handler);
    void set_menu_runtime_state(bool has_project, bool macro_running, bool macro_paused);
    bool is_headless_mode_enabled() const;    
    std::string invoke_binding(const std::string& name, const std::string& payload = "");
    /// Called on the AppKit main thread from \c applicationShouldTerminate before windows are closed.
    /// Use this to stop background threads and tear down \c WebViewWindow pointers owned by the host.
    void set_terminate_handler(std::function<void()> handler);
private:
    WebViewAppImpl* impl;
};
#endif // WEBVIEW_H