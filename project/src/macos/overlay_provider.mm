#include <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "overlay_provider.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "shared.h"

@interface OverlayBridge : NSObject <MTKViewDelegate>
@property (strong) NSWindow *window;
@property (strong) MTKView *mtkView;
@property (strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, copy) void (^renderCallback)(void);
@end

@implementation OverlayBridge

- (instancetype)init {
    if (self = [super init]) {
        [self setupNativeWindow];
        [self setupImGui];
    }
    return self;
}

- (void)setupNativeWindow {
    NSRect frame = [[[NSScreen screens] firstObject] frame];   
     
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:NSWindowStyleMaskBorderless
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
                                                 
    [self.window setOpaque:NO];
    [self.window setBackgroundColor:[NSColor clearColor]];
    [self.window setLevel:kCGOverlayWindowLevel];
    [self.window setIgnoresMouseEvents:YES]; 

    [self.window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | 
                                   NSWindowCollectionBehaviorFullScreenAuxiliary |
                                   NSWindowCollectionBehaviorIgnoresCycle];
    
    self.mtkView = [[MTKView alloc] initWithFrame:frame device:MTLCreateSystemDefaultDevice()];
    self.mtkView.delegate = self;
    self.mtkView.layer.opaque = NO;
    self.mtkView.clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    // CHANGE: Set paused back to NO so it self-refreshes at 60Hz
    self.mtkView.paused = NO; 
    self.mtkView.enableSetNeedsDisplay = NO;
    
    self.window.contentView = self.mtkView;
    self.commandQueue = [self.mtkView.device newCommandQueue];
    
    [self.window setHidesOnDeactivate:NO];

    // Ensure the window is visible and on top
    [self.window makeKeyAndOrderFront:nil];
    [self.window orderFrontRegardless];
    window_id = self.window.windowNumber;
}

- (void)setupImGui {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplOSX_Init(self.mtkView);
    ImGui_ImplMetal_Init(self.mtkView.device);
}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor* desc = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    
    // If we can't get a descriptor, just skip this frame
    if (!desc || !drawable) return;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    ImGui_ImplMetal_NewFrame(desc);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();

    if (self.renderCallback) self.renderCallback();

    ImGui::Render();
    
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:desc];
    if (encoder) {
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {}
@end

static OverlayBridge* g_overlay = nil;

void InitOverlay() {
    if (!g_overlay) g_overlay = [[OverlayBridge alloc] init];
}

void SetRenderCallback(void (*callback)(void)) {
    g_overlay.renderCallback = ^{ callback(); };
}

void UpdateOverlay() {
    @autoreleasepool {
        // This is the most important part for preventing the hang.
        // NSRunLoopCommonModes ensures the UI stays alive even if the 
        // main thread is busy with capture logic.
        NSEvent* event;
        do {
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                            inMode:NSRunLoopCommonModes
                                            dequeue:YES];
            if (event) [NSApp sendEvent:event];
        } while (event);
    }
}

void MoveOverlayToDisplay(CGDirectDisplayID display_id) {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSScreen *targetScreen = nil;
        
        // Find the matching NSScreen for the CGDirectDisplayID
        for (NSScreen *screen in [NSScreen screens]) {
            NSNumber *screenID = [screen deviceDescription][@"NSScreenNumber"];
            if ([screenID unsignedIntValue] == display_id) {
                targetScreen = screen;
                break;
            }
        }
        
        if (targetScreen) {
            NSRect screenFrame = [targetScreen frame];
            
            // Move and resize the window
            [g_overlay.window setFrame:screenFrame display:YES];
            
            // Update the MTKView bounds (always starts at 0,0 relative to the window)
            [g_overlay.mtkView setFrame:NSMakeRect(0, 0, screenFrame.size.width, screenFrame.size.height)];
            
            NSLog(@"Moved overlay to display %u with frame: %@", display_id, NSStringFromRect(screenFrame));
        } else {
            NSLog(@"[OVERLAY ERROR] Could not find NSScreen for display ID %u", display_id);
        }
    });
}