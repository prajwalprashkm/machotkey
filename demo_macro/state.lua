local M = {
  paused = false,
  phase = "idle",
  suite_running = false,

  fps = 0,
  latency_ms = 0,
  raw_fps = 0,
  dropped_frames = 0,

  bench_results = {},

  --- last frame workload timing (for HUD + push_runtime)
  workload_ops = 0,
  workload_total_us = 0,
  workload_us_per_op = 0,

  --- last metrics window: action ops/s from summed workload µs; window avg µs/op
  workload_action_label = nil,
  workload_action_hz = 0,
  workload_action_avg_us_per_op = 0,

  _metrics_phase = nil,
  _metrics_start_us = nil,
  _metrics_lat_sum_us = 0,
  _metrics_frames = 0,
  _metrics_dropped_window = 0,
  _metrics_workload_ops_sum = 0,
  _metrics_workload_us_sum = 0,

  n_frames = 0,
  last_frame_running = false,
  last_index = 0,
  start_time_us = 0,
  dropped_frames_window = 0,

  color_seed = 0,
  ui_screen_hint = nil,

  --- OpenCV: workloads.lua runs real OpenCV only after WebView opencv_ack (CPU dialog)
  opencv_acknowledged = true,

  --- Full suite: wait for first metrics window in current phase before advancing (slow phases need this)
  _suite_gen = 0,
  _suite_waiting_metrics_for = nil,
  _suite_on_first_metrics = nil,
}

return M
