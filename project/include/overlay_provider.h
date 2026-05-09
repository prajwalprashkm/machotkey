#pragma once

#include <CoreGraphics/CoreGraphics.h>

void InitOverlay();
void SetRenderCallback(void (*callback)(void));
void PollEvents();
void MoveOverlayToDisplay(CGDirectDisplayID display_id);