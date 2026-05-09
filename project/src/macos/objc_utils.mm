#include "objc_utils.h"
#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

std::string get_app_bundle_path() {
    return std::string([[[NSBundle mainBundle] bundlePath] UTF8String]);
}
const char* get_macro_runner_path(){
    return [[[[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"macro_runner"] UTF8String];
}
std::string open_project_dialog() {
    __block std::string chosen = "";
    
    auto block = ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Load Project";
        
        if([panel runModal] == NSModalResponseOK) {
            chosen = std::string(panel.URL.path.UTF8String);
        }
    };
    
    if([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
    
    return chosen;
}

bool prompt_quick_script_input(std::string& out_filename, std::string& out_code) {
    __block bool accepted = false;
    auto block = ^{
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Run Quick Script"];
        [alert setInformativeText:@"Enter a file name and script contents."];
        [alert addButtonWithTitle:@"Run"];
        [alert addButtonWithTitle:@"Cancel"];

        NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 320)];

        NSTextField* filenameLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 294, 520, 20)];
        [filenameLabel setStringValue:@"Filename"];
        [filenameLabel setEditable:NO];
        [filenameLabel setBordered:NO];
        [filenameLabel setBezeled:NO];
        [filenameLabel setDrawsBackground:NO];

        NSTextField* filenameField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 268, 520, 24)];
        [filenameField setStringValue:@"quick.lua"];

        NSTextField* codeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 242, 520, 20)];
        [codeLabel setStringValue:@"Code"];
        [codeLabel setEditable:NO];
        [codeLabel setBordered:NO];
        [codeLabel setBezeled:NO];
        [codeLabel setDrawsBackground:NO];

        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 520, 238)];
        [scroll setHasVerticalScroller:YES];
        [scroll setBorderType:NSBezelBorder];

        NSTextView* codeView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 520, 238)];
        [codeView setString:@""];
        [scroll setDocumentView:codeView];

        [accessory addSubview:filenameLabel];
        [accessory addSubview:filenameField];
        [accessory addSubview:codeLabel];
        [accessory addSubview:scroll];
        [alert setAccessoryView:accessory];

        if ([alert runModal] != NSAlertFirstButtonReturn) {
            return;
        }

        NSString* filename = [[filenameField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSString* code = [codeView string];
        if (!filename || [filename length] == 0 || !code || [code length] == 0) {
            return;
        }

        out_filename = std::string([filename UTF8String]);
        out_code = std::string([code UTF8String]);
        accepted = true;
    };

    if([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
    return accepted;
}