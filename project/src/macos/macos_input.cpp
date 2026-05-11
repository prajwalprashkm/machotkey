/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#include "utils.h"
#if defined(__APPLE__)
#include <thread>
#include <chrono>
#include "input_interface.h"
#include <ApplicationServices/ApplicationServices.h>
#include "shared.h"

InputInterface::Mouse::Point InputInterface::Mouse::get_pos() {
    CGEventRef event = CGEventCreate(NULL);
    CGPoint loc = CGEventGetLocation(event);
    CFRelease(event);
    return {loc.x, loc.y};
}

static inline void move_instant(double x, double y) {
    CGEventRef event = CGEventCreateMouseEvent(
        NULL, kCGEventMouseMoved, CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void InputInterface::Mouse::move(double target_x, double target_y, double duration) {
    if (duration <= 0) {
        move_instant(target_x, target_y);
        return;
    }

    Point start = get_pos();
    int steps = 60 * duration;
    if (steps < 1) steps = 1;
    double interval = (duration * 1000000.0) / steps;

    for (int i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;
        double next_x = start.x + (target_x - start.x) * t;
        double next_y = start.y + (target_y - start.y) * t;
        move_instant(next_x, next_y);
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(interval)));
    }
}

void InputInterface::Mouse::move_relative(double delta_x, double delta_y, double duration) {
    Point current = get_pos();
    move(current.x + delta_x, current.y + delta_y, duration);
}

void InputInterface::Mouse::send_event(double x, double y, MouseButton button, MouseEventType type){
    CGEventType cg_type;
    CGMouseButton cg_button;

    if(button == MouseButton::LEFT) {
        cg_button = kCGMouseButtonLeft;
        cg_type = (type == MouseEventType::DOWN) ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
    } else if(button == MouseButton::RIGHT) {
        cg_button = kCGMouseButtonRight;
        cg_type = (type == MouseEventType::DOWN) ? kCGEventRightMouseDown : kCGEventRightMouseUp;
    } else {
        cg_button = kCGMouseButtonCenter;
        cg_type = (type == MouseEventType::DOWN) ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
    }
    CGEventRef event = CGEventCreateMouseEvent(NULL, cg_type, CGPointMake(x, y), cg_button);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void InputInterface::Mouse::click(MouseButton button, int clicks) {
    Point pos = get_pos();
    CGEventType up_type, down_type;
    CGMouseButton cg_button;
    CGEventRef down, up;

    if(button == MouseButton::LEFT) {
        down_type = kCGEventLeftMouseDown;
        up_type   = kCGEventLeftMouseUp;
        cg_button = kCGMouseButtonLeft;
    } else if(button == MouseButton::RIGHT) {
        down_type = kCGEventRightMouseDown;
        up_type   = kCGEventRightMouseUp;
        cg_button = kCGMouseButtonRight;
    } else {
        down_type = kCGEventOtherMouseDown;
        up_type   = kCGEventOtherMouseUp;
        cg_button = kCGMouseButtonCenter;
    }

    for(int i = 1; i <= clicks; i++) {
        down = CGEventCreateMouseEvent(NULL, down_type, CGPointMake(pos.x, pos.y), cg_button);
        up = CGEventCreateMouseEvent(NULL, up_type, CGPointMake(pos.x, pos.y), cg_button);

        CGEventSetIntegerValueField(down, kCGMouseEventClickState, i);
        CGEventSetIntegerValueField(up, kCGMouseEventClickState, i);

        CGEventPost(kCGHIDEventTap, down);
        CGEventPost(kCGHIDEventTap, up);

        CFRelease(down);
        CFRelease(up);
    }
}

void InputInterface::Mouse::click(double x, double y, MouseButton button, int clicks) {
    move(x, y, 0);
    click(button, clicks);
}

void InputInterface::Keyboard::send_event(uint16_t keycode, long long flags, KeyEventType type) {
    bool is_down = (type == KeyEventType::DOWN);
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)keycode, is_down);
    
    if (event) {
        CGEventSetFlags(event, (CGEventFlags)flags);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

void InputInterface::Keyboard::press(uint16_t keycode, long long flags) {
    send_event(keycode, flags, KeyEventType::DOWN);
    send_event(keycode, flags, KeyEventType::UP);
}

void InputInterface::Keyboard::type(std::string text, InputInterface::Keyboard::TypeMode mode, long long interval_ms) {
    if(mode == TypeMode::SEQUENTIAL) {
        for(char c : text) {
            press(key_map.at(std::string(1, c)), (std::find(shift_required.begin(), shift_required.end(), std::string(1, c)) != shift_required.end()) ? kCGEventFlagMaskShift : 0);
            if(interval_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        }
        return;
    }
    CFStringRef cf_text = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
    UniChar buffer[text.length()];
    CFStringGetCharacters(cf_text, CFRangeMake(0, CFStringGetLength(cf_text)), buffer);

    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)0, true);
    CGEventKeyboardSetUnicodeString(event, CFStringGetLength(cf_text), buffer);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    CFRelease(cf_text);
}
#endif