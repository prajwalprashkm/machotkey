/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once

#ifndef INPUT_INTERFACE_H
#define INPUT_INTERFACE_H

#include <utility>
#include "utils.h"

namespace InputInterface {
    namespace Mouse {
        struct Point {
            double x, y;
        };
        enum class MouseButton{
            LEFT,
            RIGHT,
            MIDDLE
        };
        enum class MouseEventType{
            DOWN,
            UP
        };
        
        Point get_pos();
        void move(double target_x, double target_y, double duration);
        void move_relative(double delta_x, double delta_y, double duration);
        void click(double x, double y, MouseButton button, int clicks);
        void click(MouseButton button, int clicks);
        void send_event(double x, double y, MouseButton button, MouseEventType type);
    }
    namespace Keyboard {
        enum class KeyEventType{
            DOWN,
            UP
        };
        enum class TypeMode{
            SEQUENTIAL,
            STRING
        };
        
        void press(uint16_t keycode, long long flags /*CGEventFlags*/);
        void send_event(uint16_t keycode, long long flags /*CGEventFlags*/, KeyEventType type);
        void type(std::string text, InputInterface::Keyboard::TypeMode mode, long long interval_ms = 0);
    }
}

#endif