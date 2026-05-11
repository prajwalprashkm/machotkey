/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

// webview.mm
#include <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <iostream>
#include <map>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>

#include "webview.h"
#include "../../include/debug_config.h"

#if MHK_ENABLE_DEBUG_LOGS
#define DEBUG_LOG(format, ...) NSLog(@"[webview.mm DEBUG] " format, ##__VA_ARGS__)
#else
#define DEBUG_LOG(format, ...)
#endif

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface CustomSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation CustomSchemeHandler

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    NSURL *url = urlSchemeTask.request.URL;
    
    // Map the custom scheme URL to your local bundle path
    // Example: app://localhost/main/script.js -> BundlePath/main/script.js
    NSString *path = url.path;
    if ([path hasPrefix:@"/"]) path = [path substringFromIndex:1];
    
    NSURL *fileURL = [[NSBundle mainBundle] URLForResource:path withExtension:nil];
    NSData *data = [NSData dataWithContentsOfURL:fileURL];
    
    if (data) {
        // Determine MIME type (crucial for scripts/CSS to work)
        NSString *ext = [fileURL pathExtension];
        UTType *type = [UTType typeWithFilenameExtension:ext];
        NSString *mimeType = type.preferredMIMEType ?: @"application/octet-stream";
        
        NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:url
                                                                  statusCode:200
                                                                HTTPVersion:@"HTTP/1.1"
                                                                headerFields:@{@"Content-Type": mimeType,
                                                                              @"Access-Control-Allow-Origin": @"*"}];
        
        [urlSchemeTask didReceiveResponse:response];
        [urlSchemeTask didReceiveData:data];
        [urlSchemeTask didFinish];
    } else {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil]];
    }
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    // Handle cleanup if the task is cancelled
}

@end


NSURL* getResourceURL(const std::string& fileName) {
    NSString *fullName = [NSString stringWithUTF8String:fileName.c_str()];
    NSString *name = [fullName stringByDeletingPathExtension];
    NSString *ext = [fullName pathExtension];
    
    return [[NSBundle mainBundle] URLForResource:name withExtension:ext];
}

// Forward Objective-C classes
@class WebMessageHandler;
@class WebNavigationDelegate;
@class WindowDelegate;
@class WebUIDelegate;
@class AppDelegate;

// Forward C++ impls
class WebViewWindowImpl;
class WebViewAppImpl;

//
// Objective-C delegate interfaces
//
@interface WebNavigationDelegate : NSObject <WKNavigationDelegate>
@property(nonatomic, assign) WebViewWindowImpl* cppImpl;
@end

@interface WebMessageHandler : NSObject <WKScriptMessageHandler>
@property(nonatomic, assign) WebViewWindowImpl* cppImpl;
@end

@interface WindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) WebViewWindowImpl* cppImpl;
@end

// ✅ Added: UIDelegate to handle <input type="file">
@interface WebUIDelegate : NSObject <WKUIDelegate>
@property(nonatomic, assign) WebViewWindowImpl* cppImpl;
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) WebViewWindowImpl* cppImpl;
@end

@interface AppMenuTarget : NSObject
@property(nonatomic, assign) WebViewAppImpl* cppAppImpl;
- (void)onToggleHeadless:(id)sender;
- (void)onLoadProject:(id)sender;
- (void)onRunProjectMacro:(id)sender;
- (void)onRunQuickScript:(id)sender;
- (void)onStopMacro:(id)sender;
- (void)onPauseResumeMacro:(id)sender;
@end

@interface CustomWebView : WKWebView
@property (assign) BOOL isDraggable;
@end

@implementation CustomWebView
- (BOOL)acceptsFirstMouse:(NSEvent *)theEvent {
    return YES;
}

- (void)mouseDown:(NSEvent *)event {
    if (self.isDraggable) {
        // 1. Get click location in view coordinates (Bottom-Left is 0,0)
        NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
        NSRect bounds = [self bounds];
        
        CGFloat t = bounds.size.height < bounds.size.width ? bounds.size.height/5 : bounds.size.width/5; // Thickness of the draggable border
        
        // 2. Define the four border zones
        BOOL isAtLeft   = point.x <= t;
        BOOL isAtRight  = point.x >= (bounds.size.width - t);
        BOOL isAtBottom = point.y <= t;
        BOOL isAtTop    = point.y >= (bounds.size.height - t);

        // 3. If the click is in ANY of the border zones, drag the window
        if (isAtLeft || isAtRight || isAtBottom || isAtTop) {
            [self.window performWindowDragWithEvent:event];
            return; // Block the event from reaching HTML buttons in the border
        }
    }
    
    // Otherwise, let the web page handle the click (buttons, links, etc.)
    [super mouseDown:event];
}
@end

//
// C++ Implementation classes
//
class WebViewWindowImpl {
public:
    WebViewWindowImpl(long id, const std::string& title, int x, int y, int width, int height,
                      bool resizable, bool frameless,
                      std::map<std::string, std::function<std::string(const std::string&)>>* bindings);
    ~WebViewWindowImpl();
    void set_html(const std::string& html_content);
    void set_html_resource(const std::string& resource_name, const std::string& subdirectory);
    void load_project_html_file(const std::string& absolute_html_path, const std::string& absolute_project_dir);
    void load_url(const std::string& url);
    void handle_js_message(NSString* name, NSString* body);
    void handle_macro_ui_message(NSString* event_name, NSString* payload);
    void set_macro_ui_handler(std::function<void(const std::string&, const std::string&)> handler);
    bool enforces_local_file_security() const { return local_file_security_mode; }
    void close();
    void inject_script();
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
    void set_opacity(float alpha);
    void bring_to_front(bool key);
    uint32_t get_native_window_number() const;

    std::atomic<bool> ready{false};
    bool is_ready(){ return ready.load(); };
private:

    long id;
    NSWindow* __strong window;
    WKWebView* __strong webView;

    // Delegates
    WebMessageHandler* __strong messageHandler;
    WebNavigationDelegate* __strong navDelegate;
    WindowDelegate* __strong windowDelegate;

    // ✅ Added: strong reference so WebKit does not drop the UIDelegate
    WebUIDelegate* __strong uiDelegate;
    bool is_frameless_window = false;

    std::map<std::string, std::function<std::string(const std::string&)>>* shared_bindings;
    std::function<void(const std::string&, const std::string&)> macro_ui_handler;
    bool local_file_security_mode = false;
    bool cpp_bridge_enabled = true;
};

class WebViewAppImpl {
public:
    WebViewAppImpl();
    ~WebViewAppImpl();
    WebViewWindow* create_window(const std::string& title, int x, int y, int width, int height,
                                 bool resizable, bool frameless);
    void destroy_window(WebViewWindow* window);
    void bind(const std::string& name, std::function<std::string(const std::string&)> fn);
    void run();
    void run_blocking();
    void stop();
    void quit();
    void set_accessory(bool isAccessory);
    void set_menu_action_handler(std::function<void(AppMenuAction)> handler);
    void set_menu_runtime_state(bool has_project, bool macro_running, bool macro_paused);
    bool is_headless_mode_enabled() const { return headless_mode_enabled; }
    std::string invoke_binding(const std::string& name, const std::string& payload);
    void handle_menu_action(AppMenuAction action);
    void install_menu();
    std::map<std::string, std::function<std::string(const std::string&)>>* get_bindings() {
        return &bindings;
    }
    long get_next_window_id() { return nextWindowId++; }
private:
    std::map<std::string, std::function<std::string(const std::string&)>> bindings;
    std::vector<std::unique_ptr<WebViewWindow>> windows;
    std::function<void(AppMenuAction)> menu_action_handler;
    AppMenuTarget* __strong menu_target = nil;
    NSMenuItem* __strong menu_toggle_headless = nil;
    NSMenuItem* __strong menu_run_project = nil;
    NSMenuItem* __strong menu_run_quick = nil;
    NSMenuItem* __strong menu_stop = nil;
    NSMenuItem* __strong menu_pause_resume = nil;
    bool headless_mode_enabled = false;
    long nextWindowId = 0;
};

//
// Objective-C implementations
//
@implementation WebNavigationDelegate
- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    DEBUG_LOG(@"Page finished loading, injecting script");
    if (self.cppImpl) {
        self.cppImpl->inject_script();
        self.cppImpl->ready.store(true);
    }
}

- (void)webView:(WKWebView *)webView
decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction
decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    if (!self.cppImpl || !self.cppImpl->enforces_local_file_security()) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    NSURL *url = navigationAction.request.URL;
    if (!url) {
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    if ([url isFileURL] || [[url scheme] isEqualToString:@"about"]) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    DEBUG_LOG(@"Blocked non-local navigation to %@", url);
    decisionHandler(WKNavigationActionPolicyCancel);
}
@end

@implementation AppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    // Close all windows
    for (NSWindow *window in [NSApp windows]) {
        [window close];
    }
    return NSTerminateNow;
}
@end

@implementation AppMenuTarget
- (void)onToggleHeadless:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::ToggleHeadless);
}
- (void)onLoadProject:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::LoadProject);
}
- (void)onRunProjectMacro:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::RunProjectMacro);
}
- (void)onRunQuickScript:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::RunQuickScript);
}
- (void)onStopMacro:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::StopMacro);
}
- (void)onPauseResumeMacro:(id)sender {
    (void)sender;
    if (self.cppAppImpl) self.cppAppImpl->handle_menu_action(AppMenuAction::PauseResumeMacro);
}
@end

@implementation WebMessageHandler
- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    DEBUG_LOG(@"Received message with name: %@", message.name);
    if ([message.name isEqualToString:@"cpp_message_handler"]) {
        if ([message.body isKindOfClass:[NSDictionary class]]) {
            NSDictionary *messageDict = (NSDictionary *)message.body;
            NSString *funcName = messageDict[@"name"];
            NSString *funcBody = messageDict[@"body"];
            DEBUG_LOG(@"Function name: %@", funcName);
            if (self.cppImpl) {
                self.cppImpl->handle_js_message(funcName, funcBody);
            }
        } else {
            DEBUG_LOG(@"Unexpected message body: %@", message.body);
        }
    } else if ([message.name isEqualToString:@"_console_logging"]) {
        DEBUG_LOG(@"JS Log: %@", message.body);
    } else if ([message.name isEqualToString:@"macroUI"]) {
        if ([message.body isKindOfClass:[NSDictionary class]]) {
            NSDictionary *messageDict = (NSDictionary *)message.body;
            NSString *eventName = messageDict[@"event"];
            NSString *payload = messageDict[@"payload"];
            if (self.cppImpl) {
                self.cppImpl->handle_macro_ui_message(eventName, payload);
            }
        } else {
            DEBUG_LOG(@"Unexpected macroUI body: %@", message.body);
        }
    } else {
        DEBUG_LOG(@"Unexpected message handler name: %@", message.name);
    }
}
@end

@implementation WindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    DEBUG_LOG(@"Window will close");
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    return YES;
}
@end

// ✅ Added: File picker support
@implementation WebUIDelegate
- (void)webView:(WKWebView*)webView
runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
 initiatedByFrame:(WKFrameInfo*)frame
completionHandler:(void (^)(NSArray<NSURL*> *urls))completionHandler
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;

    [panel beginWithCompletionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            completionHandler(panel.URLs);
        } else {
            completionHandler(nil);
        }
    }];
}
// ✅ JS alert("message")
- (void)webView:(WKWebView *)webView
runJavaScriptAlertPanelWithMessage:(NSString *)message
 initiatedByFrame:(WKFrameInfo *)frame
completionHandler:(void (^)(void))completionHandler
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = message ?: @"";
    [alert addButtonWithTitle:@"OK"];
    [alert beginSheetModalForWindow:webView.window
                  completionHandler:^(NSModalResponse returnCode) {
        completionHandler();
    }];
}

// ✅ JS confirm("message") → returns true/false
- (void)webView:(WKWebView *)webView
runJavaScriptConfirmPanelWithMessage:(NSString *)message
 initiatedByFrame:(WKFrameInfo *)frame
completionHandler:(void (^)(BOOL))completionHandler
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = message ?: @"";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    [alert beginSheetModalForWindow:webView.window
                  completionHandler:^(NSModalResponse returnCode) {
        completionHandler(returnCode == NSAlertFirstButtonReturn);
    }];
}

// ✅ JS prompt("msg", "default") → returns string or null
- (void)webView:(WKWebView *)webView
runJavaScriptTextInputPanelWithPrompt:(NSString *)prompt
defaultText:(NSString *)defaultText
 initiatedByFrame:(WKFrameInfo *)frame
completionHandler:(void (^)(NSString * _Nullable result))completionHandler
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = prompt ?: @"";

    NSSecureTextField *input = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0,0, 250, 24)];
    input.stringValue = defaultText ?: @"";

    alert.accessoryView = input;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    [alert beginSheetModalForWindow:webView.window
                  completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSAlertFirstButtonReturn)
            completionHandler(input.stringValue);
        else
            completionHandler(nil);
    }];
}
@end

//
// C++ wrapper implementations
//

// WebViewWindow thin wrapper
WebViewWindow::WebViewWindow(WebViewAppImpl* parent, const std::string& title, 
                             int x, int y, int width, int height,
                             bool resizable, bool frameless) {
    impl = new WebViewWindowImpl(parent->get_next_window_id(), title, x, y, width, height,
                                 resizable, frameless, parent->get_bindings());
}

WebViewWindow::~WebViewWindow() {
    delete static_cast<WebViewWindowImpl*>(impl);
    impl = nullptr;
}

void WebViewWindow::set_html(const std::string& html_content) {
    static_cast<WebViewWindowImpl*>(impl)->set_html(html_content);
}

void WebViewWindow::set_html_resource(const std::string& resource_name, const std::string& subdirectory) {
    static_cast<WebViewWindowImpl*>(impl)->set_html_resource(resource_name, subdirectory);
}

void WebViewWindow::load_project_html_file(const std::string& absolute_html_path, const std::string& absolute_project_dir) {
    static_cast<WebViewWindowImpl*>(impl)->load_project_html_file(absolute_html_path, absolute_project_dir);
}

void WebViewWindow::load_url(const std::string& url) {
    static_cast<WebViewWindowImpl*>(impl)->load_url(url);
}

void WebViewWindow::set_macro_ui_handler(std::function<void(const std::string&, const std::string&)> handler) {
    static_cast<WebViewWindowImpl*>(impl)->set_macro_ui_handler(std::move(handler));
}

void WebViewWindow::close() {
    static_cast<WebViewWindowImpl*>(impl)->close();
}

void WebViewWindow::send_to_js(const std::string& js) {
    static_cast<WebViewWindowImpl*>(impl)->send_to_js(js);
}

void WebViewWindow::set_title(const std::string& title) {
    static_cast<WebViewWindowImpl*>(impl)->set_title(title);
}

void WebViewWindow::center() {
    static_cast<WebViewWindowImpl*>(impl)->center();
}

void WebViewWindow::set_ignores_mouse(bool ignores) {
    static_cast<WebViewWindowImpl*>(impl)->set_ignores_mouse(ignores);
}

void WebViewWindow::set_size(int width, int height) {
    static_cast<WebViewWindowImpl*>(impl)->set_size(width, height);
}

void WebViewWindow::set_position(int x, int y) {
    static_cast<WebViewWindowImpl*>(impl)->set_position(x, y);
}

void WebViewWindow::get_position_async(std::function<void(int, int)> callback) {
    static_cast<WebViewWindowImpl*>(impl)->get_position_async(callback);
}

void WebViewWindow::minimize() {
    static_cast<WebViewWindowImpl*>(impl)->minimize();
}

void WebViewWindow::maximize() {
    static_cast<WebViewWindowImpl*>(impl)->maximize();
}

void WebViewWindow::exit_fullscreen() {
    static_cast<WebViewWindowImpl*>(impl)->exit_fullscreen();
}

void WebViewWindow::show() {
    static_cast<WebViewWindowImpl*>(impl)->show();
}

void WebViewWindow::hide() {
    static_cast<WebViewWindowImpl*>(impl)->hide();
}

void WebViewWindow::set_opacity(float alpha) {
    static_cast<WebViewWindowImpl*>(impl)->set_opacity(alpha);
}

void WebViewWindow::bring_to_front(bool key) {
    static_cast<WebViewWindowImpl*>(impl)->bring_to_front(key);
}

uint32_t WebViewWindow::get_native_window_number() const {
    return static_cast<WebViewWindowImpl*>(impl)->get_native_window_number();
}

bool WebViewWindow::is_ready() {
    return static_cast<WebViewWindowImpl*>(impl)->is_ready();
}

// WebViewApp thin wrapper
WebViewApp::WebViewApp() : impl(new WebViewAppImpl()) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        impl->install_menu();

        // Set your delegate
        AppDelegate* delegate = [[AppDelegate alloc] init];
        [NSApp setDelegate:delegate];
    }
}

WebViewApp::~WebViewApp() {
    delete impl;
}

WebViewWindow* WebViewApp::create_window(const std::string& title, int x, int y, 
                                         int width, int height, bool resizable, 
                                         bool frameless) {
    return impl->create_window(title, x, y, width, height, resizable, frameless);
}

void WebViewApp::destroy_window(WebViewWindow* window) {
    impl->destroy_window(window);
}

void WebViewApp::bind(const std::string& name, std::function<std::string(const std::string&)> fn) {
    impl->bind(name, fn);
}

void WebViewApp::run() {
    impl->run();
}

void WebViewApp::run_blocking() {
    impl->run_blocking();
}

void WebViewApp::stop() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
        NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                             location:NSMakePoint(0, 0)
                                        modifierFlags:0
                                            timestamp:0
                                         windowNumber:0
                                              context:nil
                                              subtype:0
                                                data1:0
                                                data2:0];
        [NSApp postEvent:event atStart:YES];
    });
}

void WebViewApp::quit() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}

void WebViewApp::set_accessory(bool isAccessory){
    impl->set_accessory(isAccessory);
}

void WebViewApp::set_menu_action_handler(std::function<void(AppMenuAction)> handler) {
    impl->set_menu_action_handler(std::move(handler));
}

void WebViewApp::set_menu_runtime_state(bool has_project, bool macro_running, bool macro_paused) {
    impl->set_menu_runtime_state(has_project, macro_running, macro_paused);
}

bool WebViewApp::is_headless_mode_enabled() const {
    return impl->is_headless_mode_enabled();
}

std::string WebViewApp::invoke_binding(const std::string& name, const std::string& payload) {
    return impl->invoke_binding(name, payload);
}

//
// WebViewWindowImpl methods
//
WebViewWindowImpl::WebViewWindowImpl(long id, const std::string& title, int x, int y, 
                                     int width, int height, bool resizable, bool frameless,
                                     std::map<std::string, std::function<std::string(const std::string&)>>* bindings)
    : id(id), is_frameless_window(frameless), shared_bindings(bindings) {
    @autoreleasepool {
        NSScreen* mainScreen = [NSScreen mainScreen];
        NSRect screenFrame = [mainScreen frame];
        
        CGFloat windowY = screenFrame.size.height - y - height;
        NSRect windowFrame = NSMakeRect(x, windowY, width, height);

        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
        
        if (!frameless) {
            styleMask |= NSWindowStyleMaskMiniaturizable;
            if (resizable) {
                styleMask |= NSWindowStyleMaskResizable;
            }
        } else {
            styleMask = NSWindowStyleMaskBorderless;
        }

        if(!frameless){ 
            window = [[NSWindow alloc]
                initWithContentRect:windowFrame
                    styleMask:styleMask
                        backing:NSBackingStoreBuffered
                        defer:NO];
        }else{
            window = [[NSPanel alloc]
                initWithContentRect:windowFrame
                    styleMask:NSWindowStyleMaskNonactivatingPanel | NSWindowStyleMaskBorderless
                        backing:NSBackingStoreBuffered
                        defer:NO];
        }
        
        [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
        [window setReleasedWhenClosed:NO];
        
        if (!frameless) {
            [window setBackgroundColor:[NSColor windowBackgroundColor]];
            // Ensure drag behavior is initialized immediately for titled macro UI windows.
            [window setMovable:YES];
            [window setMovableByWindowBackground:NO];
            [window setTitleVisibility:NSWindowTitleVisible];
            [window setTitlebarAppearsTransparent:NO];
        } else {
            [window setOpaque:NO];
            [window setBackgroundColor:[NSColor clearColor]];
            //[window setHasShadow:YES];
        }
        
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];

        /*CustomSchemeHandler* schemeHandler = [[CustomSchemeHandler alloc] init];
        [config setURLSchemeHandler:schemeHandler forURLScheme:@"com.machotkey.machotkey"];*/

        messageHandler = [[WebMessageHandler alloc] init];
        messageHandler.cppImpl = this;
        
        [config.userContentController addScriptMessageHandler:messageHandler name:@"cpp_message_handler"];
        [config.userContentController addScriptMessageHandler:messageHandler name:@"_console_logging"];
        [config.userContentController addScriptMessageHandler:messageHandler name:@"macroUI"];

        NSString* logScript = @"window.onerror = (msg, url, line, col, error) => { "
                              @"  window.webkit.messageHandlers._console_logging.postMessage('ERROR: ' + url + ' - ' + msg + ' @ ' + line + ':' + col); "
                              @"}; "
                              @"console.log = (msg) => { "
                              @"  window.webkit.messageHandlers._console_logging.postMessage('LOG: ' + msg); "
                              @"}; ";

        WKUserScript* logUserScript = [[WKUserScript alloc] initWithSource:logScript
                                                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                           forMainFrameOnly:NO];
        [config.userContentController addUserScript:logUserScript];

        NSString* cspScript = @"(function(){"
                              @"if (window.location.protocol !== 'file:') return;"
                              @"var doc=document;"
                              @"var root=doc.documentElement;"
                              @"if(!root) return;"
                              @"var head=doc.head;"
                              @"if(!head){"
                              @"  head=doc.createElement('head');"
                              @"  root.insertBefore(head, root.firstChild);"
                              @"}"
                              @"if(doc.querySelector('meta[http-equiv=\"Content-Security-Policy\"]')) return;"
                              @"var meta=doc.createElement('meta');"
                              @"meta.httpEquiv='Content-Security-Policy';"
                              @"meta.content=\"default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'\";"
                              @"head.appendChild(meta);"
                              @"})();";
        WKUserScript* cspUserScript = [[WKUserScript alloc] initWithSource:cspScript
                                                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                           forMainFrameOnly:YES];
        [config.userContentController addUserScript:cspUserScript];

        NSRect webFrame = NSMakeRect(0, 0, width, height);
        webView = [[CustomWebView alloc] initWithFrame:webFrame configuration:config];
        
        if (frameless) {
            webView.wantsLayer = YES;
            webView.layer.cornerRadius = 10.0;
            webView.layer.masksToBounds = YES;

            ((CustomWebView*)webView).isDraggable = YES;

            // Allows clicks to pass through transparent areas of the webview 
            // and reach the windows behind it.
            [window setIgnoresMouseEvents:NO]; 
            
            // This tells the window to use the alpha channel of the view 
            // to determine where it can be clicked.
            [window setAcceptsMouseMovedEvents:YES];

            [window setLevel:kCGOverlayWindowLevel];

            [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | 
                                   NSWindowCollectionBehaviorFullScreenAuxiliary |
                                   NSWindowCollectionBehaviorIgnoresCycle |
                                   NSWindowCollectionBehaviorTransient |
                                   NSWindowCollectionBehaviorStationary];

            [window setHidesOnDeactivate:NO];
            [window makeKeyAndOrderFront:nil];
            [window orderFrontRegardless];
        }else{
            [window setCollectionBehavior: NSWindowCollectionBehaviorFullScreenPrimary | 
                                NSWindowCollectionBehaviorFullScreenAllowsTiling |
                                NSWindowCollectionBehaviorManaged];
        }

        [webView setValue:@(NO) forKey:@"drawsBackground"];
        webView.underPageBackgroundColor = [NSColor clearColor];

        navDelegate = [[WebNavigationDelegate alloc] init];
        navDelegate.cppImpl = this;
        webView.navigationDelegate = navDelegate;

        windowDelegate = [[WindowDelegate alloc] init];
        windowDelegate.cppImpl = this;
        window.delegate = windowDelegate;

        // ✅ Add UIDelegate for file picker support
        uiDelegate = [[WebUIDelegate alloc] init];
        uiDelegate.cppImpl = this;
        webView.UIDelegate = uiDelegate;

        [window setContentView:webView];
        [window makeKeyAndOrderFront:nil];
        if (!frameless) {
            [[NSRunningApplication currentApplication]
                activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
            [NSApp activateIgnoringOtherApps:YES];
            [window makeMainWindow];
            [window makeKeyWindow];
            [window orderFrontRegardless];
            [window displayIfNeeded];
            // On some macOS builds, titled WebKit windows do not become draggable
            // by title bar until the first manual resize. Trigger an equivalent
            // no-op frame commit to initialize drag hit-testing immediately.
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!window) return;
                NSRect frame = [window frame];
                [window setFrame:frame display:YES];
                [window displayIfNeeded];
            });
        }
    }
}

WebViewWindowImpl::~WebViewWindowImpl() {
    @autoreleasepool {
        if (webView) {
            [webView.configuration.userContentController removeScriptMessageHandlerForName:@"cpp_message_handler"];
            [webView.configuration.userContentController removeScriptMessageHandlerForName:@"_console_logging"];
            [webView.configuration.userContentController removeScriptMessageHandlerForName:@"macroUI"];
        }
        [window close];
    }
}

void WebViewWindowImpl::set_html(const std::string& html_content) {
    auto load_block = ^{
        @autoreleasepool {
            local_file_security_mode = false;
            NSString* nsHtml = [NSString stringWithUTF8String:html_content.c_str()];
            NSURL *baseUrl = [[NSBundle mainBundle] resourceURL];
            DEBUG_LOG("using base url: %@", baseUrl);
            [webView loadHTMLString:nsHtml baseURL:baseUrl];
        }
    };
    if ([NSThread isMainThread]) load_block();
    else dispatch_sync(dispatch_get_main_queue(), load_block);
}

void WebViewWindowImpl::set_html_resource(const std::string& resource_name, const std::string& subdirectory) {
    auto load_block = ^{
        @autoreleasepool {
            local_file_security_mode = false;
            NSBundle *bundle = NSBundle.mainBundle;

            NSURL *htmlURL =
            [[NSBundle mainBundle] URLForResource:[NSString stringWithUTF8String:resource_name.c_str()]
                    withExtension:@"html"
                        subdirectory:[NSString stringWithUTF8String:subdirectory.c_str()]];

            NSURL *readAccessURL =
            [[bundle resourceURL] URLByAppendingPathComponent:
                [NSString stringWithUTF8String:subdirectory.c_str()]];
            
            DEBUG_LOG("Loading HTML resource from URL: %@", htmlURL);
            DEBUG_LOG("With read access to URL: %@", readAccessURL);

            [webView loadFileURL:htmlURL allowingReadAccessToURL:readAccessURL];
        }
    };
    if ([NSThread isMainThread]) load_block();
    else dispatch_sync(dispatch_get_main_queue(), load_block);
}

void WebViewWindowImpl::load_project_html_file(const std::string& absolute_html_path, const std::string& absolute_project_dir) {
    auto load_block = ^{
        @autoreleasepool {
            local_file_security_mode = true;
            NSString* htmlPath = [NSString stringWithUTF8String:absolute_html_path.c_str()];
            NSString* projectPath = [NSString stringWithUTF8String:absolute_project_dir.c_str()];
            NSURL* htmlURL = [NSURL fileURLWithPath:htmlPath isDirectory:NO];
            NSURL* readAccessURL = [NSURL fileURLWithPath:projectPath isDirectory:YES];

            DEBUG_LOG(@"Loading project HTML from %@", htmlURL);
            DEBUG_LOG(@"Read access root %@", readAccessURL);
            [webView loadFileURL:htmlURL allowingReadAccessToURL:readAccessURL];
            if (([window styleMask] & NSWindowStyleMaskTitled) != 0) {
                [window setMovable:YES];
                [window setMovableByWindowBackground:NO];
                NSRect frame = [window frame];
                [window setFrame:frame display:YES];
                [window displayIfNeeded];
            }
        }
    };
    if ([NSThread isMainThread]) load_block();
    else dispatch_sync(dispatch_get_main_queue(), load_block);
}

void WebViewWindowImpl::load_url(const std::string& url) {
    DEBUG_LOG(@"using url: %s", url.c_str());
    auto load_block = ^{
        @autoreleasepool {
            local_file_security_mode = false;
            NSString* nsUrl = [NSString stringWithUTF8String:url.c_str()];
            NSURL* urlObj = [NSURL URLWithString:nsUrl];
            NSURLRequest* request = [NSURLRequest requestWithURL:urlObj];
            [webView loadRequest:request];
        }
    };
    if ([NSThread isMainThread]) load_block();
    else dispatch_sync(dispatch_get_main_queue(), load_block);
}

void WebViewWindowImpl::inject_script() {
    @autoreleasepool {
        NSString* script = nil;
        if (cpp_bridge_enabled) {
            script =
                @"(function(){"
                @"if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.cpp_message_handler) {"
                @"  window.callCpp = function(name, body) {"
                @"    window.webkit.messageHandlers.cpp_message_handler.postMessage({name: name, body: body});"
                @"  };"
                @"}"
                @"if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.macroUI) {"
                @"  window.macroUI = window.macroUI || {};"
                @"  window.macroUI.emit = function(event, payload) {"
                @"    window.webkit.messageHandlers.macroUI.postMessage({event: String(event || ''), payload: payload == null ? '' : String(payload)});"
                @"  };"
                @"}"
                @"})();";
        } else {
            script =
                @"(function(){"
                @"if (window.callCpp) {"
                @"  try { delete window.callCpp; } catch(_) { window.callCpp = undefined; }"
                @"}"
                @"if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.macroUI) {"
                @"  window.macroUI = window.macroUI || {};"
                @"  window.macroUI.emit = function(event, payload) {"
                @"    window.webkit.messageHandlers.macroUI.postMessage({event: String(event || ''), payload: payload == null ? '' : String(payload)});"
                @"  };"
                @"}"
                @"})();";
        }
        [webView evaluateJavaScript:script completionHandler:nil];
    }
}

std::string escapeStr(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size() * 1.2); // Reserve some extra space
    
    for (unsigned char c : str) {
        switch (c) {
            case '\'': escaped += "\\'"; break;
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\0': escaped += "\\0"; break;
            default:
                // Handle control characters and special Unicode
                if (c < 0x20 || c == 0x7F) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    escaped += buf;
                } else {
                    escaped += c;
                }
                break;
        }
    }
    
    return escaped;
}

void WebViewWindowImpl::handle_js_message(NSString* name, NSString* body) {
    @autoreleasepool {
        if (!cpp_bridge_enabled) return;
        if (!name || !body) return;
        std::string cppName = [name UTF8String];
        if (shared_bindings->count(cppName)) {
            std::string jsMessage = [body UTF8String];
            std::string result = shared_bindings->at(cppName)(jsMessage);
            
            std::string escapedName = escapeStr(cppName);
            std::string escaped = escapeStr(result);
            
            NSString* jsName = [NSString stringWithUTF8String:escapedName.c_str()];
            NSString* jsResult = [NSString stringWithUTF8String:escaped.c_str()];
            NSString* jsCode = [NSString stringWithFormat:@"if(window.cpp_response) window.cpp_response('%@', '%@');", jsName, jsResult];
            [webView evaluateJavaScript:jsCode completionHandler:nil];
        } else {
            DEBUG_LOG(@"No binding found for function: %s", cppName.c_str());
        }
    }
}

void WebViewWindowImpl::handle_macro_ui_message(NSString* event_name, NSString* payload) {
    @autoreleasepool {
        if (!event_name || ![event_name isKindOfClass:[NSString class]]) return;
        if (!macro_ui_handler) return;
        std::string event = [event_name UTF8String];
        std::string payload_str;
        if (payload && [payload isKindOfClass:[NSString class]]) {
            payload_str = [payload UTF8String];
        }
        macro_ui_handler(event, payload_str);
    }
}

void WebViewWindowImpl::set_macro_ui_handler(std::function<void(const std::string&, const std::string&)> handler) {
    cpp_bridge_enabled = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        [webView.configuration.userContentController removeScriptMessageHandlerForName:@"cpp_message_handler"];
    });
    macro_ui_handler = std::move(handler);
}

void WebViewWindowImpl::close() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window close];
    });
}

void WebViewWindowImpl::send_to_js(const std::string& js) {
    NSString *jsString = [NSString stringWithUTF8String:js.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
        [webView evaluateJavaScript:jsString completionHandler:^(NSObject *result, NSError *error) {
            if (error) {
                DEBUG_LOG(@"Error executing JavaScript: %@", error);
            }
        }];
    });
    
}

void WebViewWindowImpl::set_title(const std::string& title) {
    NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
        [window setTitle:nsTitle];
    });
}

void WebViewWindowImpl::center() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window center];
    });
}

void WebViewWindowImpl::set_ignores_mouse(bool ignores) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window setIgnoresMouseEvents:ignores];
    });
}

void WebViewWindowImpl::set_size(int width, int height) {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSRect frame = [window frame];
        frame.size.width = width;
        frame.size.height = height;
        
        [window setFrame:frame display:YES animate:YES];
        // Ensure the webview fills the new dimensions
        [webView setFrame:NSMakeRect(0, 0, width, height)];
    });
}

void WebViewWindowImpl::set_position(int x, int y) {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSScreen* primaryScreen = [[NSScreen screens] firstObject];
        CGFloat primaryHeight = primaryScreen.frame.size.height;
        NSRect windowFrame = [window frame];

        CGFloat cocoaX = (CGFloat)x;

        CGFloat cocoaY = primaryHeight - (CGFloat)y - windowFrame.size.height;

        [window setFrameOrigin:NSMakePoint(cocoaX, cocoaY)];
    });
}

void WebViewWindowImpl::get_position_async(std::function<void(int, int)> callback) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!window) return;

        NSScreen* primaryScreen = [[NSScreen screens] firstObject];
        CGFloat primaryHeight = primaryScreen.frame.size.height;
        NSRect windowFrame = [window frame];

        // 1. X is global and intuitive
        int globalX = (int)windowFrame.origin.x;

        // 2. Calculate Y relative to the primary screen's top edge
        // GlobalTopLeftY = PrimaryHeight - CocoaY - WindowHeight
        int globalY = (int)(primaryHeight - windowFrame.origin.y - windowFrame.size.height);

        if (callback) {
            callback(globalX, globalY);
        }
    });
}

void WebViewWindowImpl::minimize() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window miniaturize:nil];
    });
}

void WebViewWindowImpl::maximize() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window zoom:nil];
    });
}

void WebViewWindowImpl::exit_fullscreen() {
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([window isZoomed]) {
            [window zoom:nil];
        }
        if (([window styleMask] & NSWindowStyleMaskFullScreen) != 0) {
            // If it's in full-screen mode, exit it
            [window toggleFullScreen:nil];
        }
    });
}

void WebViewWindowImpl::show() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSRunningApplication currentApplication]
            activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
        [window makeMainWindow];
        [window displayIfNeeded];
    });
}

void WebViewWindowImpl::hide() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window orderOut:nil];
    });
}

void WebViewWindowImpl::set_opacity(float alpha) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [window setAlphaValue:alpha];
    });
}

void WebViewWindowImpl::bring_to_front(bool key) {
    if([NSThread isMainThread] == true){
        [[NSRunningApplication currentApplication]
            activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
        [NSApp activateIgnoringOtherApps:YES];
        if (key) {
            [window makeKeyAndOrderFront:nil];
            [window makeMainWindow];
        } else {
            [window orderFrontRegardless];
        }
        [window displayIfNeeded];
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), ^{
        [[NSRunningApplication currentApplication]
            activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
        [NSApp activateIgnoringOtherApps:YES];
        if (key) {
            [window makeKeyAndOrderFront:nil];
            [window makeMainWindow];
        } else {
            [window orderFrontRegardless];
        }
        [window displayIfNeeded];
    });
}

uint32_t WebViewWindowImpl::get_native_window_number() const {
    __block uint32_t window_number = 0;
    void (^read_block)(void) = ^{
        if (!window) return;
        window_number = static_cast<uint32_t>([window windowNumber]);
    };
    if ([NSThread isMainThread]) {
        read_block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), read_block);
    }
    return window_number;
}

//
// WebViewAppImpl methods
//
static NSString* const kHeadlessModeDefaultsKey = @"machotkey.headless_mode_enabled";

WebViewAppImpl::WebViewAppImpl() {
    headless_mode_enabled = [[NSUserDefaults standardUserDefaults] boolForKey:kHeadlessModeDefaultsKey];
}
WebViewAppImpl::~WebViewAppImpl() {}

void WebViewAppImpl::install_menu() {
    @autoreleasepool {
        NSMenu *mainMenu = [[NSMenu alloc] init];
        NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
        [mainMenu addItem:appMenuItem];

        NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Machotkey"];
        menu_target = [[AppMenuTarget alloc] init];
        menu_target.cppAppImpl = this;

        menu_toggle_headless = [[NSMenuItem alloc] initWithTitle:@"Headless Mode"
                                                           action:@selector(onToggleHeadless:)
                                                    keyEquivalent:@""];
        [menu_toggle_headless setTarget:menu_target];
        [menu_toggle_headless setState:headless_mode_enabled ? NSControlStateValueOn : NSControlStateValueOff];
        [appMenu addItem:menu_toggle_headless];
        [appMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem *loadProject = [[NSMenuItem alloc] initWithTitle:@"Load Project..."
                                                              action:@selector(onLoadProject:)
                                                       keyEquivalent:@"o"];
        [loadProject setTarget:menu_target];
        [appMenu addItem:loadProject];

        menu_run_project = [[NSMenuItem alloc] initWithTitle:@"Run Project Macro"
                                                      action:@selector(onRunProjectMacro:)
                                               keyEquivalent:@"r"];
        [menu_run_project setTarget:menu_target];
        [appMenu addItem:menu_run_project];

        menu_run_quick = [[NSMenuItem alloc] initWithTitle:@"Run Quick Script..."
                                                    action:@selector(onRunQuickScript:)
                                             keyEquivalent:@"R"];
        [menu_run_quick setTarget:menu_target];
        [appMenu addItem:menu_run_quick];

        menu_pause_resume = [[NSMenuItem alloc] initWithTitle:@"Pause Macro"
                                                       action:@selector(onPauseResumeMacro:)
                                                keyEquivalent:@"p"];
        [menu_pause_resume setTarget:menu_target];
        [appMenu addItem:menu_pause_resume];

        menu_stop = [[NSMenuItem alloc] initWithTitle:@"Stop Macro"
                                               action:@selector(onStopMacro:)
                                        keyEquivalent:@"s"];
        [menu_stop setTarget:menu_target];
        [appMenu addItem:menu_stop];

        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit Machotkey"
                           action:@selector(terminate:)
                    keyEquivalent:@"q"];

        [appMenuItem setSubmenu:appMenu];
        [NSApp setMainMenu:mainMenu];
        set_menu_runtime_state(false, false, false);
    }
}

WebViewWindow* WebViewAppImpl::create_window(const std::string& title, int x, int y, 
                                             int width, int height, bool resizable, 
                                             bool frameless) {
    __block WebViewWindow* result = nullptr;
    void (^work)(void) = ^{
        @autoreleasepool {
            auto window = std::make_unique<WebViewWindow>(this, title, x, y, width, height, 
                                                           resizable, frameless);
            result = window.get();
            windows.push_back(std::move(window));
        }
    };
    if ([NSThread isMainThread]) {
        work();
    } else {
        dispatch_sync(dispatch_get_main_queue(), work);
    }
    return result;
}

void WebViewAppImpl::destroy_window(WebViewWindow* target) {
    if (!target) return;
    void (^work)(void) = ^{
        auto it = std::find_if(windows.begin(), windows.end(), [target](const std::unique_ptr<WebViewWindow>& window) {
            return window.get() == target;
        });
        if (it != windows.end()) {
            windows.erase(it);
        }
    };
    if ([NSThread isMainThread]) {
        work();
    } else {
        dispatch_sync(dispatch_get_main_queue(), work);
    }
}

void WebViewAppImpl::bind(const std::string& name, std::function<std::string(const std::string&)> fn) {
    bindings[name] = fn;
    DEBUG_LOG(@"Binding function: %s", name.c_str());
}

std::string WebViewAppImpl::invoke_binding(const std::string& name, const std::string& payload) {
    auto it = bindings.find(name);
    if (it == bindings.end()) return "error: Unknown binding";
    return it->second(payload);
}

void WebViewAppImpl::set_menu_action_handler(std::function<void(AppMenuAction)> handler) {
    menu_action_handler = std::move(handler);
}

void WebViewAppImpl::set_menu_runtime_state(bool has_project, bool macro_running, bool macro_paused) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (menu_run_project) [menu_run_project setEnabled:(has_project && !macro_running)];
        if (menu_run_quick) [menu_run_quick setEnabled:(!macro_running)];
        if (menu_stop) [menu_stop setEnabled:macro_running];
        if (menu_pause_resume) {
            [menu_pause_resume setEnabled:macro_running];
            [menu_pause_resume setTitle:(macro_paused ? @"Resume Macro" : @"Pause Macro")];
        }
    });
}

void WebViewAppImpl::handle_menu_action(AppMenuAction action) {
    if (action == AppMenuAction::ToggleHeadless) {
        headless_mode_enabled = !headless_mode_enabled;
        [[NSUserDefaults standardUserDefaults] setBool:headless_mode_enabled forKey:kHeadlessModeDefaultsKey];
        if (menu_toggle_headless) {
            [menu_toggle_headless setState:headless_mode_enabled ? NSControlStateValueOn : NSControlStateValueOff];
        }
    }
    if (menu_action_handler) {
        menu_action_handler(action);
    }
}

void WebViewAppImpl::run() {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

void WebViewAppImpl::run_blocking() {
    @autoreleasepool {
        [NSApp run];
    }
}

void WebViewAppImpl::stop() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
    });
}

void WebViewAppImpl::quit() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}

void WebViewAppImpl::set_accessory(bool isAccessory){
    if([NSThread isMainThread] == true){
        @autoreleasepool {
            if(isAccessory){
                [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            }else{
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            }
        }
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            if(isAccessory){
                [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            }else{
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            }
        }
    });
}