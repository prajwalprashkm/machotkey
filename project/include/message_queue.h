/*
 * Copyright (c) Prajwal Prashanth. All rights reserved.
 *
 * This source code is licensed under the source-available license 
 * found in the LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <queue>
#include <mutex>
#include <string>
#include <utility>
#include <cstdint>
#include "ipc_protocol.h"
#include "utils.h"

class MessageQueue {
private:
    static constexpr size_t kMaxKeyEvents = 4096;
    static constexpr size_t kMaxUiEvents = 1024;

    std::queue<KeyCombo> key_events;
    struct UIEventRecord {
        uint32_t window_id;
        std::string event_name;
        std::string payload;
    };
    std::queue<UIEventRecord> ui_events;
    uint64_t dropped_key_events = 0;
    uint64_t dropped_ui_events = 0;
    std::mutex mtx;

public:
    void push_event(KeyCombo data) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key_events.size() >= kMaxKeyEvents) {
            key_events.pop(); // Drop oldest to keep latest activity.
            ++dropped_key_events;
        }
        key_events.push(data);
    }

    bool pop_event(KeyCombo& out) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key_events.empty()) return false;
        out = key_events.front();
        key_events.pop();
        return true;
    }

    void push_ui_event(uint32_t window_id, const std::string& event_name, const std::string& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        if (ui_events.size() >= kMaxUiEvents) {
            ui_events.pop(); // Drop oldest queued UI event on overload.
            ++dropped_ui_events;
        }
        ui_events.push(UIEventRecord{window_id, event_name, payload});
    }

    bool pop_ui_event(uint32_t& window_id, std::string& event_name, std::string& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        if (ui_events.empty()) return false;
        auto event = std::move(ui_events.front());
        ui_events.pop();
        window_id = event.window_id;
        event_name = std::move(event.event_name);
        payload = std::move(event.payload);
        return true;
    }

    std::pair<uint64_t, uint64_t> take_dropped_counts() {
        std::lock_guard<std::mutex> lock(mtx);
        const auto out = std::make_pair(dropped_key_events, dropped_ui_events);
        dropped_key_events = 0;
        dropped_ui_events = 0;
        return out;
    }
};