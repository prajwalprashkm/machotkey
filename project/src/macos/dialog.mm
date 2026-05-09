#import <AppKit/AppKit.h>
#include "dialog.h"

@interface PermissionPanel : NSPanel
@end

@implementation PermissionPanel
- (BOOL)canBecomeKeyWindow  { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

// Minimal delegate to receive button actions and stop the modal session
@interface PermissionHandler : NSObject
@property (nonatomic, assign) BOOL result;
- (void)allow:(id)sender;
- (void)deny:(id)sender;
@end

@implementation PermissionHandler
- (void)allow:(id)sender {
    self.result = YES;
    [NSApp stopModalWithCode:1];
}
- (void)deny:(id)sender {
    self.result = NO;
    [NSApp stopModalWithCode:0];
}
@end

@interface SystemPermissionHandler : NSObject
@property (nonatomic, assign) BOOL proceed;
- (void)proceedAction:(id)sender;
- (void)cancelAction:(id)sender;
@end

@implementation SystemPermissionHandler
- (void)proceedAction:(id)sender {
    self.proceed = YES;
    [NSApp stopModalWithCode:1];
}
- (void)cancelAction:(id)sender {
    self.proceed = NO;
    [NSApp stopModalWithCode:0];
}
@end

@interface RateApprovalHandler : NSObject
@property (nonatomic, assign) RateApprovalChoice choice;
- (void)allowOnce:(id)sender;
- (void)allowAlways:(id)sender;
- (void)deny:(id)sender;
@end

@implementation RateApprovalHandler
- (void)allowOnce:(id)sender {
    self.choice = RateApprovalChoice::AllowOnce;
    [NSApp stopModalWithCode:1];
}
- (void)allowAlways:(id)sender {
    self.choice = RateApprovalChoice::AllowAlways;
    [NSApp stopModalWithCode:2];
}
- (void)deny:(id)sender {
    self.choice = RateApprovalChoice::Deny;
    [NSApp stopModalWithCode:0];
}
@end

bool show_dialog(const std::string& title, const std::string& description) {
    __block bool user_allowed = false;

    auto code_block = ^{
        const CGFloat W      = 420;
        const CGFloat pad    = 24;
        const CGFloat btnH   = 32;
        const CGFloat btnW   = 100;
        const CGFloat gap    = 10;
        const CGFloat titleH = 22;

        NSString *bodyStr  = [NSString stringWithUTF8String:description.c_str()];
        NSFont   *bodyFont = [NSFont systemFontOfSize:13];
        NSRect    bodyBounds = [bodyStr
            boundingRectWithSize:NSMakeSize(W - pad*2, CGFLOAT_MAX)
            options:NSStringDrawingUsesLineFragmentOrigin
            attributes:@{ NSFontAttributeName: bodyFont }
            context:nil];
        CGFloat bodyH    = ceil(bodyBounds.size.height);
        CGFloat bodyTopY = pad + titleH + 10;
        CGFloat H        = bodyTopY + bodyH + pad + btnH + pad;

        PermissionPanel *panel = [[PermissionPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, W, H)
            styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
            backing:NSBackingStoreBuffered
            defer:NO];

        [panel setLevel:NSPopUpMenuWindowLevel];
        [panel setCollectionBehavior:
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorFullScreenAuxiliary];
        [panel setHidesOnDeactivate:NO];
        [panel setOpaque:NO];
        [panel setHasShadow:YES];
        [panel center];

        NSView *cv = [panel contentView];
        cv.wantsLayer = YES;
        cv.layer.backgroundColor = [[NSColor windowBackgroundColor] CGColor];
        cv.layer.cornerRadius    = 12.0;
        cv.layer.masksToBounds   = YES;

        NSTextField *titleLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - pad - titleH, W - pad*2, titleH)];
        titleLabel.stringValue     = [NSString stringWithUTF8String:title.c_str()];
        titleLabel.font            = [NSFont boldSystemFontOfSize:15];
        titleLabel.bezeled         = NO;
        titleLabel.editable        = NO;
        titleLabel.drawsBackground = NO;
        [cv addSubview:titleLabel];

        NSTextField *bodyLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - bodyTopY - bodyH, W - pad*2, bodyH)];
        bodyLabel.stringValue          = bodyStr;
        bodyLabel.font                 = bodyFont;
        bodyLabel.bezeled              = NO;
        bodyLabel.editable             = NO;
        bodyLabel.drawsBackground      = NO;
        bodyLabel.lineBreakMode        = NSLineBreakByWordWrapping;
        bodyLabel.maximumNumberOfLines = 0;
        [cv addSubview:bodyLabel];

        PermissionHandler *handler = [[PermissionHandler alloc] init];

        NSButton *allowBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW, pad, btnW, btnH)];
        allowBtn.title         = @"Allow";
        allowBtn.bezelStyle    = NSBezelStyleRounded;
        allowBtn.keyEquivalent = @"\r";
        allowBtn.target        = handler;
        allowBtn.action        = @selector(allow:);
        [cv addSubview:allowBtn];

        NSButton *denyBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW*2 - gap, pad, btnW, btnH)];
        denyBtn.title         = @"Deny";
        denyBtn.bezelStyle    = NSBezelStyleRounded;
        denyBtn.keyEquivalent = @"\033";
        denyBtn.target        = handler;
        denyBtn.action        = @selector(deny:);
        [cv addSubview:denyBtn];

        [NSApp activateIgnoringOtherApps:YES];
        [panel makeKeyAndOrderFront:nil];
        [panel orderFrontRegardless];

        // runModalForWindow respects the level/collectionBehavior we set
        // because the window is already ordered front before we call it.
        NSModalResponse response = [NSApp runModalForWindow:panel];

        user_allowed = (response == 1);
        [panel orderOut:nil];
    };

    if ([NSThread isMainThread]) code_block();
    else dispatch_sync(dispatch_get_main_queue(), code_block);

    return user_allowed;
}

bool show_system_permission_dialog(const std::string& title, const std::string& description) {
    __block bool should_proceed = false;

    auto code_block = ^{
        const CGFloat W = 560;
        const CGFloat pad = 24;
        const CGFloat titleH = 24;
        const CGFloat subtitleH = 18;
        const CGFloat btnH = 34;
        const CGFloat btnW = 132;
        const CGFloat gap = 12;

        NSString *bodyStr = [NSString stringWithUTF8String:description.c_str()];
        NSFont *bodyFont = [NSFont systemFontOfSize:13];
        NSRect bodyBounds = [bodyStr
            boundingRectWithSize:NSMakeSize(W - pad * 2, CGFLOAT_MAX)
            options:NSStringDrawingUsesLineFragmentOrigin
            attributes:@{ NSFontAttributeName: bodyFont }
            context:nil];
        CGFloat bodyH = ceil(bodyBounds.size.height);
        CGFloat bodyTopY = pad + titleH + subtitleH + 14;
        CGFloat H = bodyTopY + bodyH + pad + btnH + pad;

        PermissionPanel *panel = [[PermissionPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, W, H)
            styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
            backing:NSBackingStoreBuffered
            defer:NO];

        [panel setLevel:NSPopUpMenuWindowLevel];
        [panel setCollectionBehavior:
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorFullScreenAuxiliary];
        [panel setHidesOnDeactivate:NO];
        [panel setOpaque:NO];
        [panel setHasShadow:YES];
        [panel center];

        NSView *cv = [panel contentView];
        cv.wantsLayer = YES;
        cv.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.09 green:0.12 blue:0.18 alpha:0.98] CGColor];
        cv.layer.cornerRadius = 14.0;
        cv.layer.masksToBounds = YES;
        cv.layer.borderWidth = 1.0;
        cv.layer.borderColor = [[NSColor colorWithCalibratedRed:0.19 green:0.25 blue:0.38 alpha:1.0] CGColor];

        NSTextField *titleLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - pad - titleH, W - pad * 2, titleH)];
        titleLabel.stringValue = [NSString stringWithUTF8String:title.c_str()];
        titleLabel.font = [NSFont boldSystemFontOfSize:16];
        titleLabel.textColor = [NSColor colorWithCalibratedRed:0.95 green:0.97 blue:1.0 alpha:1.0];
        titleLabel.bezeled = NO;
        titleLabel.editable = NO;
        titleLabel.drawsBackground = NO;
        [cv addSubview:titleLabel];

        NSTextField *subtitleLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - pad - titleH - subtitleH - 4, W - pad * 2, subtitleH)];
        subtitleLabel.stringValue = @"Native macOS permission prompt will open next";
        subtitleLabel.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
        subtitleLabel.textColor = [NSColor colorWithCalibratedRed:0.38 green:0.80 blue:0.64 alpha:1.0];
        subtitleLabel.bezeled = NO;
        subtitleLabel.editable = NO;
        subtitleLabel.drawsBackground = NO;
        [cv addSubview:subtitleLabel];

        NSTextField *bodyLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - bodyTopY - bodyH, W - pad * 2, bodyH)];
        bodyLabel.stringValue = bodyStr;
        bodyLabel.font = bodyFont;
        bodyLabel.textColor = [NSColor colorWithCalibratedRed:0.82 green:0.87 blue:0.95 alpha:1.0];
        bodyLabel.bezeled = NO;
        bodyLabel.editable = NO;
        bodyLabel.drawsBackground = NO;
        bodyLabel.lineBreakMode = NSLineBreakByWordWrapping;
        bodyLabel.maximumNumberOfLines = 0;
        [cv addSubview:bodyLabel];

        SystemPermissionHandler *handler = [[SystemPermissionHandler alloc] init];

        NSButton *continueBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW, pad, btnW, btnH)];
        continueBtn.title = @"Continue";
        continueBtn.bordered = NO;
        continueBtn.wantsLayer = YES;
        continueBtn.layer.cornerRadius = 8.0;
        continueBtn.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.20 green:0.83 blue:0.61 alpha:1.0] CGColor];
        continueBtn.layer.borderColor = [[NSColor colorWithCalibratedRed:0.15 green:0.66 blue:0.50 alpha:1.0] CGColor];
        continueBtn.layer.borderWidth = 1.0;
        continueBtn.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
        continueBtn.keyEquivalent = @"\r";
        continueBtn.target = handler;
        continueBtn.action = @selector(proceedAction:);
        [cv addSubview:continueBtn];

        NSButton *cancelBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW * 2 - gap, pad, btnW, btnH)];
        cancelBtn.title = @"Not Now";
        cancelBtn.bordered = NO;
        cancelBtn.wantsLayer = YES;
        cancelBtn.layer.cornerRadius = 8.0;
        cancelBtn.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.16 green:0.20 blue:0.29 alpha:1.0] CGColor];
        cancelBtn.layer.borderColor = [[NSColor colorWithCalibratedRed:0.25 green:0.31 blue:0.45 alpha:1.0] CGColor];
        cancelBtn.layer.borderWidth = 1.0;
        cancelBtn.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
        cancelBtn.keyEquivalent = @"\033";
        cancelBtn.target = handler;
        cancelBtn.action = @selector(cancelAction:);
        [cv addSubview:cancelBtn];

        [NSApp activateIgnoringOtherApps:YES];
        [panel makeKeyAndOrderFront:nil];
        [panel orderFrontRegardless];

        NSModalResponse response = [NSApp runModalForWindow:panel];
        should_proceed = (response == 1 && handler.proceed);
        [panel orderOut:nil];
    };

    if ([NSThread isMainThread]) code_block();
    else dispatch_sync(dispatch_get_main_queue(), code_block);

    return should_proceed;
}

RateApprovalChoice show_rate_approval_dialog(const std::string& title, const std::string& description) {
    __block RateApprovalChoice choice = RateApprovalChoice::Deny;

    auto code_block = ^{
        const CGFloat W      = 520;
        const CGFloat pad    = 24;
        const CGFloat btnH   = 32;
        const CGFloat btnW   = 120;
        const CGFloat gap    = 10;
        const CGFloat titleH = 22;

        NSString *bodyStr  = [NSString stringWithUTF8String:description.c_str()];
        NSFont   *bodyFont = [NSFont systemFontOfSize:13];
        NSRect    bodyBounds = [bodyStr
            boundingRectWithSize:NSMakeSize(W - pad*2, CGFLOAT_MAX)
            options:NSStringDrawingUsesLineFragmentOrigin
            attributes:@{ NSFontAttributeName: bodyFont }
            context:nil];
        CGFloat bodyH    = ceil(bodyBounds.size.height);
        CGFloat bodyTopY = pad + titleH + 10;
        CGFloat H        = bodyTopY + bodyH + pad + btnH + pad;

        PermissionPanel *panel = [[PermissionPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, W, H)
            styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
            backing:NSBackingStoreBuffered
            defer:NO];

        [panel setLevel:NSPopUpMenuWindowLevel];
        [panel setCollectionBehavior:
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorFullScreenAuxiliary];
        [panel setHidesOnDeactivate:NO];
        [panel setOpaque:NO];
        [panel setHasShadow:YES];
        [panel center];

        NSView *cv = [panel contentView];
        cv.wantsLayer = YES;
        cv.layer.backgroundColor = [[NSColor windowBackgroundColor] CGColor];
        cv.layer.cornerRadius    = 12.0;
        cv.layer.masksToBounds   = YES;

        NSTextField *titleLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - pad - titleH, W - pad*2, titleH)];
        titleLabel.stringValue     = [NSString stringWithUTF8String:title.c_str()];
        titleLabel.font            = [NSFont boldSystemFontOfSize:15];
        titleLabel.bezeled         = NO;
        titleLabel.editable        = NO;
        titleLabel.drawsBackground = NO;
        [cv addSubview:titleLabel];

        NSTextField *bodyLabel = [[NSTextField alloc] initWithFrame:
            NSMakeRect(pad, H - bodyTopY - bodyH, W - pad*2, bodyH)];
        bodyLabel.stringValue          = bodyStr;
        bodyLabel.font                 = bodyFont;
        bodyLabel.bezeled              = NO;
        bodyLabel.editable             = NO;
        bodyLabel.drawsBackground      = NO;
        bodyLabel.lineBreakMode        = NSLineBreakByWordWrapping;
        bodyLabel.maximumNumberOfLines = 0;
        [cv addSubview:bodyLabel];

        RateApprovalHandler *handler = [[RateApprovalHandler alloc] init];

        NSButton *alwaysBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW, pad, btnW, btnH)];
        alwaysBtn.title         = @"Allow Always";
        alwaysBtn.bezelStyle    = NSBezelStyleRounded;
        alwaysBtn.target        = handler;
        alwaysBtn.action        = @selector(allowAlways:);
        [cv addSubview:alwaysBtn];

        NSButton *onceBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW*2 - gap, pad, btnW, btnH)];
        onceBtn.title         = @"Allow Once";
        onceBtn.bezelStyle    = NSBezelStyleRounded;
        onceBtn.keyEquivalent = @"\r";
        onceBtn.target        = handler;
        onceBtn.action        = @selector(allowOnce:);
        [cv addSubview:onceBtn];

        NSButton *denyBtn = [[NSButton alloc] initWithFrame:
            NSMakeRect(W - pad - btnW*3 - gap*2, pad, btnW, btnH)];
        denyBtn.title         = @"Keep Current";
        denyBtn.bezelStyle    = NSBezelStyleRounded;
        denyBtn.keyEquivalent = @"\033";
        denyBtn.target        = handler;
        denyBtn.action        = @selector(deny:);
        [cv addSubview:denyBtn];

        [NSApp activateIgnoringOtherApps:YES];
        [panel makeKeyAndOrderFront:nil];
        [panel orderFrontRegardless];

        [NSApp runModalForWindow:panel];

        choice = handler.choice;
        [panel orderOut:nil];
    };

    if ([NSThread isMainThread]) code_block();
    else dispatch_sync(dispatch_get_main_queue(), code_block);

    return choice;
}