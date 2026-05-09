#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>
#include <vector>
#include <string>

// ImGui headers (Ensure these paths match your project structure)
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "macos_ui.h"

// --- Objective-C Private Interface ---
@interface GuiPanelInstance : NSObject <MTKViewDelegate>
@property (strong) NSPanel *panel;
@property (strong) MTKView *mtkView;
@property (strong) id<MTLCommandQueue> commandQueue;
@property (assign) ImGuiContext* imguiContext;
@property (assign) std::vector<GuiWidget> widgets;
@end

@implementation GuiPanelInstance

- (instancetype)initWithTitle:(NSString*)title frame:(NSRect)frame {
    self = [super init];
    if (self) {
        // 1. Create the Panel
        // NSWindowStyleMaskNonactivatingPanel is key: it won't steal focus from other apps
        _panel = [[NSPanel alloc] initWithContentRect:frame
                                            styleMask:NSWindowStyleMaskTitled | 
                                                      NSWindowStyleMaskClosable | 
                                                      NSWindowStyleMaskResizable |
                                                      NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
        
        [_panel setTitle:title];
        [_panel setLevel:NSStatusWindowLevel]; // Floats above regular windows and HUD
        [_panel setFloatingPanel:YES];
        [_panel setBecomesKeyOnlyIfNeeded:YES];
        [_panel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | 
                                    NSWindowCollectionBehaviorFullScreenAuxiliary];
        
        // 2. Setup Metal View
        _mtkView = [[MTKView alloc] initWithFrame:frame device:MTLCreateSystemDefaultDevice()];
        _mtkView.delegate = self;
        _mtkView.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0); // Slight dark grey background
        _panel.contentView = _mtkView;
        _commandQueue = [_mtkView.device newCommandQueue];

        // 3. Setup Dedicated ImGui Context
        _imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(_imguiContext);
        
        // Initialize ImGui Backends for this specific view
        ImGui_ImplOSX_Init(_mtkView);
        ImGui_ImplMetal_Init(_mtkView.device);
        
        // Load a default font or style if desired
        ImGui::StyleColorsDark();

        [_panel makeKeyAndOrderFront:nil];
    }
    return self;
}

- (void)drawInMTKView:(MTKView *)view {
    // CRITICAL: Switch to this window's specific ImGui context before drawing
    ImGui::SetCurrentContext(_imguiContext);

    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor == nil) return;

    // Start ImGui Frame
    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();

    // 4. UI Layout Logic
    // We create a "Window" that fills the entire NSPanel area
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(view.bounds.size.width, view.bounds.size.height));
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("PanelContent", nullptr, window_flags)) {
        for (const auto& widget : _widgets) {
            if (widget.type == "button") {
                if (ImGui::Button(widget.label.c_str(), ImVec2(-FLT_MIN, 0))) {
                    // TODO: Callback to C++ / IPC to notify button click
                    printf("Button %d clicked: %s\n", widget.id, widget.label.c_str());
                }
            } 
            else if (widget.type == "text") {
                ImGui::TextWrapped("%s", widget.label.c_str());
            }
            else if (widget.type == "checkbox") {
                // Note: In a real app, you'd sync this bool back to the macro runner
                bool val = widget.bool_val;
                if (ImGui::Checkbox(widget.label.c_str(), &val)) {
                    // Notify toggle
                }
            }
            ImGui::Spacing();
        }
    }
    ImGui::End();

    // 5. Rendering
    ImGui::Render();
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [renderEncoder pushDebugGroup:@"ImGui Render"];

    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);

    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // Handle window resizing if necessary
}

- (void)dealloc {
    ImGui::SetCurrentContext(_imguiContext);
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext(_imguiContext);
}

@end

// --- C++ Bridge Implementation ---

GuiPanelBridge::GuiPanelBridge(const std::string& title, int x, int y, int w, int h) {
    @autoreleasepool {
        NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
        NSRect frame = NSMakeRect(x, y, w, h);
        
        GuiPanelInstance* instance = [[GuiPanelInstance alloc] initWithTitle:nsTitle frame:frame];
        self = (__bridge_retained void*)instance;
    }
}

GuiPanelBridge::~GuiPanelBridge() {
    @autoreleasepool {
        // Transfer ownership back to Obj-C to allow it to be deallocated
        GuiPanelInstance* instance = (__bridge_transfer GuiPanelInstance*)self;
        [instance.panel close];
        instance = nil;
    }
}

void GuiPanelBridge::UpdateWidgets(const std::vector<GuiWidget>& widgets) {
    GuiPanelInstance* instance = (__bridge GuiPanelInstance*)self;
    instance.widgets = widgets;
}

bool GuiPanelBridge::IsClosed() {
    GuiPanelInstance* instance = (__bridge GuiPanelInstance*)self;
    return ![instance.panel isVisible];
}