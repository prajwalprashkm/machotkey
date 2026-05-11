/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#ifndef GUI_PANEL_BRIDGE_H
#define GUI_PANEL_BRIDGE_H

#include <string>
#include <vector>

// This struct is pure C++, safe for main.cpp
struct GuiWidget {
    int id;
    std::string type;
    std::string label;
    bool bool_val;
};

class GuiPanelBridge {
public:
    // Constructor handles the heavy lifting of spawning the NSPanel
    GuiPanelBridge(const std::string& title, int x, int y, int w, int h);
    ~GuiPanelBridge();

    // Call this whenever the Macro Runner sends new data
    void UpdateWidgets(const std::vector<GuiWidget>& widgets);
    
    // Check if the user clicked the close button
    bool IsClosed();

private:
    void* self; // Opaque pointer to the actual Obj-C instance
};

class MacroUIManager {
    std::vector<std::unique_ptr<GuiPanelBridge>> active_panels;

public:
    void AddWindow(const std::string& title, int x, int y, int w, int h) {
        active_panels.push_back(std::make_unique<GuiPanelBridge>(title, x, y, w, h));
    }

    void Tick() {
        // Remove windows that the user closed manually
        active_panels.erase(
            std::remove_if(active_panels.begin(), active_panels.end(),
                [](const auto& p) { return p->IsClosed(); }),
            active_panels.end()
        );
    }
    
    void UpdateData(int window_index, const std::vector<GuiWidget>& data) {
        if(window_index < active_panels.size()) {
            active_panels[window_index]->UpdateWidgets(data);
        }
    }
};

#endif