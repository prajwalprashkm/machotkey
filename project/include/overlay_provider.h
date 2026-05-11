/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <CoreGraphics/CoreGraphics.h>

void InitOverlay();
void SetRenderCallback(void (*callback)(void));
void PollEvents();
void MoveOverlayToDisplay(CGDirectDisplayID display_id);