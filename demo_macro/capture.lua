local workloads = require("workloads")
local config_ui = require("config_ui")

local M = {}

function M.start(state, config, ctx, geometry, _colors_mod, _regions_mod)
  state.start_time_us = system.get_time("us")

  system.screen.canvas.rect({ x = 20, y = 18, w = 570, h = 300 }, 0x111827CC, { fill = 0x111827CC, id = "hud_bg" })
  system.screen.canvas.text("Machotkey demo / capture benchmark", 32, 32, 0xFFFFFFFF, { id = "hud_title" })
  system.screen.canvas.text("Ctrl+P pause/resume   Ctrl+Q quit   Phase + suite from WebView panel", 32, 54, 0x9CA3C8FF, { id = "hud_keys" })

  system.screen.begin_capture(function()
    if state.paused then
      state.last_frame_running = false
      return
    end

    local ts_now = system.screen.get_current_timestamp()
    local latency_us = ts_now - system.screen.timestamp
    local latency_ms = latency_us / 1000

    if state.last_frame_running then
      state.dropped_frames = state.dropped_frames + 1
      return
    end
    state.last_frame_running = true

    state.n_frames = state.n_frames + 1
    local skipped = system.screen.index - state.last_index - 1
    if skipped > 0 then
      state.dropped_frames = state.dropped_frames + skipped
      state.dropped_frames_window = state.dropped_frames_window + skipped
    end
    state.last_index = system.screen.index

    -- Next metrics window starts on the first frame *after* a rollover (see below). Using the previous
    -- window's last frame start here made `dt` include one extra full callback (huge for `color`).
    if state._metrics_start_us == nil then
      state._metrics_start_us = ts_now
    end

    workloads.run_phase(state, config, ctx, state.phase)
    local ts_after_work = system.screen.get_current_timestamp()

    if state._metrics_phase ~= state.phase then
      state._metrics_phase = state.phase
      state._metrics_lat_sum_us = 0
      state._metrics_frames = 0
      state._metrics_dropped_window = 0
      state._metrics_workload_ops_sum = 0
      state._metrics_workload_us_sum = 0
      state._metrics_start_us = ts_now
    end

    state._metrics_frames = state._metrics_frames + 1
    state._metrics_lat_sum_us = state._metrics_lat_sum_us + latency_us
    state._metrics_dropped_window = state._metrics_dropped_window + skipped
    state._metrics_workload_ops_sum = (state._metrics_workload_ops_sum or 0) + (state.workload_ops or 0)
    state._metrics_workload_us_sum = (state._metrics_workload_us_sum or 0) + (state.workload_total_us or 0)

    if state._metrics_frames >= config.METRICS_WINDOW_FRAMES then
      -- Wall time from start of first callback in the window through end of last callback's workload.
      local dt = math.max(1, ts_after_work - state._metrics_start_us)
      local fps = state._metrics_frames * 1000000 / dt
      local lat_ms = (state._metrics_lat_sum_us / state._metrics_frames) / 1000
      local raw_fps = (state._metrics_frames + state._metrics_dropped_window) * 1000000 / dt

      local ops_sum = state._metrics_workload_ops_sum or 0
      local us_sum = state._metrics_workload_us_sum or 0
      local action_hz = 0
      local action_avg_us = 0
      if ops_sum > 0 then
        action_avg_us = us_sum / ops_sum
        if us_sum > 0 then
          action_hz = ops_sum * 1000000 / us_sum
        end
      end
      local action_label = workloads.action_label(state.phase)

      state.fps = fps
      state.latency_ms = lat_ms
      state.raw_fps = raw_fps
      state.workload_action_label = action_label
      state.workload_action_hz = action_hz
      state.workload_action_avg_us_per_op = action_avg_us

      state.bench_results[state.phase] = {
        callbacks_per_sec = fps,
        callback_raw_fps = raw_fps,
        latency_ms = lat_ms,
        workload_ops_last_batch = state.workload_ops,
        workload_us_per_op_last_batch = state.workload_us_per_op,
        workload_total_ms_last_batch = (state.workload_total_us or 0) / 1000,
        action_label = action_label,
        action_ops_per_sec = action_hz,
        action_avg_us_per_op = action_avg_us,
      }
      config_ui.push_runtime(state)

      if state.suite_running and state._suite_waiting_metrics_for == state.phase then
        state._suite_waiting_metrics_for = nil
        local cb = state._suite_on_first_metrics
        state._suite_on_first_metrics = nil
        if type(cb) == "function" then
          cb()
        end
      end

      state._metrics_frames = 0
      state._metrics_lat_sum_us = 0
      state._metrics_start_us = nil
      state._metrics_dropped_window = 0
      state._metrics_workload_ops_sum = 0
      state._metrics_workload_us_sum = 0
    end

    if system.screen.index % 24 == 0 then
      local colors = require("color")
      local lf = math.min(1, state.latency_ms / 12)
      local lc = colors.lerpRGBA(0x34D399FF, 0xF87171FF, lf)
      system.screen.canvas.text(string.format("Phase: %s", state.phase), 32, 84, 0xE5E7EBFF, { id = "hud_phase" })
      system.screen.canvas.text(
        string.format("Capture callbacks/s %.1f (raw %.1f)   Latency %.2f ms", state.fps, state.raw_fps, state.latency_ms),
        32,
        108,
        lc,
        { id = "hud_fps" }
      )
      system.screen.canvas.text(
        string.format("Dropped frames (total): %d", state.dropped_frames),
        32,
        132,
        0x9CA3C8FF,
        { id = "hud_drop" }
      )
      system.screen.canvas.text(
        string.format(
          "Last batch: %d ops  |  %.1f us/op  |  %.1f ms",
          state.workload_ops or 0,
          state.workload_us_per_op or 0,
          (state.workload_total_us or 0) / 1000
        ),
        32,
        156,
        0x6EE7B7FF,
        { id = "hud_work" }
      )
      do
        local al = state.workload_action_label
        local ah = state.workload_action_hz or 0
        local aa = state.workload_action_avg_us_per_op or 0
        local line
        if al and ah > 0 then
          line = string.format(
            "Action: %s   %.0f/s (summed workload time)   avg %.1f us/op",
            al,
            ah,
            aa
          )
        else
          line = "Action: — (idle, awaiting OpenCV OK, or no window yet)"
        end
        system.screen.canvas.text(line, 32, 180, 0x93C5FDFF, { id = "hud_action" })
      end
      local ru, rr, rv = 0, 0, 0
      if system.stats and system.stats.get_info then
        local st = system.stats.get_info("mb")
        if type(st) == "table" then
          ru = tonumber(st.cpu) or 0
          rr = tonumber(st.ram) or 0
          rv = tonumber(st.vmem) or 0
        end
      end
      system.screen.canvas.text(
        string.format("Runner: CPU %.1f%%   RAM %.1f MB   VRAM %.1f MB", ru, rr, rv),
        32,
        204,
        0xFBBF24FF,
        { id = "hud_runner" }
      )
    end

    state.last_frame_running = false
  end, {
    fps = config.TARGET_FPS,
    region = geometry.round_rect(ctx.capture_region),
  })
end

return M
