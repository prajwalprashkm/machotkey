local M = {}

function M.start(state, config, ctx)
  state.click_window_start_ms = system.get_time("ms")
  system._create_task(function()
    while true do
      system.wait(1, "ms")

      if state.paused or not state.automation_enabled then
        goto continue
      end

      if not state.target_seen then
        goto continue
      end

      local now = system.get_time("ms")
      if now - state.last_automation_ms < config.AUTOMATION_INTERVAL_MS then
        goto continue
      end

      state.last_automation_ms = now

      local click_x = ctx.click_x
      local click_y = ctx.click_y
      if state.target_point then
        click_x = state.target_point.x
        click_y = state.target_point.y
      elseif state.ui_target_hint then
        click_x = state.ui_target_hint.x
        click_y = state.ui_target_hint.y
      end

      system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.DOWN, click_x, click_y)
      system.wait(8, "ms")
      system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.UP, click_x, click_y)
      system.keyboard.press(config.AUTOMATION_KEY)

      state.click_count_window = (state.click_count_window or 0) + 1
      local elapsed = now - (state.click_window_start_ms or now)
      if elapsed >= 1000 then
        state.clicks_per_sec = state.click_count_window * 1000 / math.max(1, elapsed)
        state.click_count_window = 0
        state.click_window_start_ms = now
      end

      ::continue::
    end
  end)
end

return M
