local workloads = require("workloads")
local config_ui = require("config_ui")

local M = {}

function M.start(state, config, ctx, geometry, _colors_mod, _regions_mod)
  state.start_time_us = system.get_time("us")

  system.screen.canvas.rect({ x = 20, y = 18, w = 520, h = 248 }, 0x111827CC, { fill = 0x111827CC, id = "hud_bg" })
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

    workloads.run_phase(state, config, ctx, state.phase)

    if state._metrics_phase ~= state.phase then
      state._metrics_phase = state.phase
      state._metrics_lat_sum_us = 0
      state._metrics_frames = 0
      state._metrics_dropped_window = 0
      state._metrics_start_us = ts_now
    end

    state._metrics_frames = state._metrics_frames + 1
    state._metrics_lat_sum_us = state._metrics_lat_sum_us + latency_us
    state._metrics_dropped_window = state._metrics_dropped_window + skipped

    if state._metrics_frames >= config.METRICS_WINDOW_FRAMES then
      local dt = math.max(1, ts_now - state._metrics_start_us)
      local fps = state._metrics_frames * 1000000 / dt
      local lat_ms = (state._metrics_lat_sum_us / state._metrics_frames) / 1000
      local raw_fps = (state._metrics_frames + state._metrics_dropped_window) * 1000000 / dt

      state.fps = fps
      state.latency_ms = lat_ms
      state.raw_fps = raw_fps
      state.bench_results[state.phase] = {
        fps = fps,
        latency_ms = lat_ms,
        raw_fps = raw_fps,
        workload_ops = state.workload_ops,
        workload_us_per_op = state.workload_us_per_op,
        workload_total_ms = (state.workload_total_us or 0) / 1000,
      }
      config_ui.push_runtime(state)

      state._metrics_frames = 0
      state._metrics_lat_sum_us = 0
      state._metrics_start_us = ts_now
      state._metrics_dropped_window = 0
    end

    if system.screen.index % 24 == 0 then
      local colors = require("color")
      local lf = math.min(1, state.latency_ms / 12)
      local lc = colors.lerpRGBA(0x34D399FF, 0xF87171FF, lf)
      system.screen.canvas.text(string.format("Phase: %s", state.phase), 32, 84, 0xE5E7EBFF, { id = "hud_phase" })
      system.screen.canvas.text(
        string.format("FPS %.1f (raw %.1f)   Latency %.2f ms", state.fps, state.raw_fps, state.latency_ms),
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
          "Workload: %d ops  |  %.1f us/op  |  batch %.1f ms",
          state.workload_ops or 0,
          state.workload_us_per_op or 0,
          (state.workload_total_us or 0) / 1000
        ),
        32,
        156,
        0x6EE7B7FF,
        { id = "hud_work" }
      )
    end

    state.last_frame_running = false
  end, {
    fps = config.TARGET_FPS,
    region = geometry.round_rect(ctx.capture_region),
  })
end

return M
