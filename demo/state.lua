local M = {
  paused = true,
  automation_enabled = false,
  request_snapshot = false,

  n_frames = 0,
  dropped_frames = 0,
  dropped_frames_window = 0,
  last_frame_running = false,
  last_index = 0,
  start_time_us = 0,
  last_window_frames = 0,

  target_seen = false,
  target_point = nil,
  ui_scan_region = nil,
  ui_target_hint = nil,

  last_automation_ms = 0,
  click_count_window = 0,
  click_window_start_ms = 0,
  clicks_per_sec = 0,
}

return M
