local M = {}

function M.start(state, config, ctx, colors, ui, geometry)
  state.start_time_us = system.get_time("us")

  system.screen.canvas.rect({ x = 28, y = 24, w = 450, h = 210 }, 0x111827AA, { fill = 0x111827AA, id = "demo_bg" })
  system.screen.canvas.text("Feature Demo Macro", 40, 40, 0xFFFFFFFF, { id = "title" })
  system.screen.canvas.text("Ctrl+P pause, Ctrl+A auto, Ctrl+S mark snapshot, Ctrl+Q quit", 40, 64, 0xA7F3D0FF, { id = "keys" })

  system.screen.begin_capture(function()
    local ts_now = system.screen.get_current_timestamp()
    local latency_ms = (ts_now - system.screen.timestamp) / 1000
    
    if state.last_frame_running then
      state.dropped_frames = state.dropped_frames + 1
      return
    end
    state.last_frame_running = true

    state.n_frames = state.n_frames + 1
    local skipped = system.screen.index - state.last_index - 1
    if skipped > 0 then
      state.dr0illopped_frames = state.dropped_frames + skipped
      state.dropped_frames_window = state.dropped_frames_window + skipped
    end
    state.last_index = system.screen.index
    
    local scan_region = state.ui_scan_region or ctx.scan_region

    local match = system.screen.find_color(
      config.TARGET_R,
      config.TARGET_G,
      config.TARGET_B,
      config.SCAN_TOLERANCE,
      scan_region
    )

    state.target_seen = match ~= nil
    if match then
      state.target_point = {
        x = match.x+10,
        y = match.y+10,
      }
    else
      state.target_point = nil
    end

    if system.screen.index % 24 == 0 then
      local now_us = system.get_time("us")
      local dt = math.max(1, now_us - state.start_time_us)
      local fps = (state.n_frames - state.last_window_frames) * 1000000 / dt
      local raw_fps = (state.n_frames + state.dropped_frames_window - state.last_window_frames) * 1000000 / dt
      state.start_time_us = now_us
      state.last_window_frames = state.n_frames
      state.dropped_frames_window = 0

      local usage = system.stats.get_info("mb")

      local fps_factor = math.min(1, fps / config.TARGET_FPS)
      local fps_color = colors.lerp_rgba(0xEF4444FF, 0x10B981FF, fps_factor)

      local latency_factor = math.min(1, latency_ms / config.MAX_LATENCY_MS)
      local latency_color = colors.lerp_rgba(0x10B981FF, 0xEF4444FF, latency_factor)

      local cpu_factor = math.min(1, usage.cpu / config.MAX_CPU)
      local cpu_color = colors.lerp_rgba(0x10B981FF, 0xEF4444FF, cpu_factor)

      local ram_factor = math.min(1, usage.ram / config.MAX_RAM)
      local ram_color = colors.lerp_rgba(0x10B981FF, 0xEF4444FF, ram_factor)

      system.screen.canvas.text(string.format("FPS %.1f (raw %.1f)", fps, raw_fps), 40, 90, fps_color, { id = "fps" })
      system.screen.canvas.text(string.format("Latency %.3f ms", latency_ms), 40, 114, latency_color, { id = "latency" })
      system.screen.canvas.text(string.format("Dropped %d", state.dropped_frames), 40, 138, 0xFFFFFFFF, { id = "dropped" })
      system.screen.canvas.text(string.format("CPU %.2f%%", usage.cpu), 40, 162, cpu_color, { id = "cpu" })
      system.screen.canvas.text(string.format("RAM %.2f MB", usage.ram), 40, 186, ram_color, { id = "ram" })

      local status = state.paused and "PAUSED" or (state.automation_enabled and "AUTO ON" or "RUNNING")
      local status_color = state.paused and 0xF59E0BFF or (state.automation_enabled and 0x22C55EFF or 0x60A5FAFF)
      system.screen.canvas.text(status, 360, 40, status_color, { id = "status" })

      ui.push_runtime(state, {
        fps = fps,
        latency_ms = latency_ms,
        dropped = state.dropped_frames,
        cpu = usage.cpu,
        ram = usage.ram,
        target_seen = state.target_seen,
        clicks_per_sec = state.clicks_per_sec or 0,
      })
    end
       
    if state.target_seen and state.target_point then
      local px = state.target_point.x + ctx.capture_region.x
      local py = state.target_point.y + ctx.capture_region.y
    end


    if state.request_snapshot then
      -- Demo hook: mark a snapshot request without writing files every frame.
      system.screen.canvas.text("SNAPSHOT FLAGGED", 300, 186, 0xFACC15FF, { id = "snap" })
      state.request_snapshot = false
    end

    state.last_frame_running = false
  end, {
    fps = config.TARGET_FPS,
    region = geometry.round_rect(ctx.capture_region),
  })
end

return M
