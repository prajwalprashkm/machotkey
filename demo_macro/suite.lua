--[[
  Automated walk through benchmark phases (idle → color → OCR → …).
  After each phase switch we wait for one full metrics window (METRICS_WINDOW_FRAMES)
  before starting the dwell timer — slow phases (color, OCR) may not reach 30 callbacks
  within SUITE_PHASE_MS alone, so results were missing from bench_results / WebView.
  Then we dwell SUITE_PHASE_MS before the next phase (same stabilization as before).
  OpenCV: timer chain starts after WebView opencv_ack, then the same metrics + dwell apply.
]]

local M = {}

local ORDER = { "idle", "color", "ocr_fast", "ocr_accurate", "input", "opencv", "fs" }

--- After first metrics rollover in `state.phase`, wait SUITE_PHASE_MS then advance to next_index.
local function arm_next_after_metrics_and_dwell(state, config, config_ui, next_index)
  state._suite_gen = (state._suite_gen or 0) + 1
  local my_gen = state._suite_gen
  state._suite_waiting_metrics_for = state.phase
  state._suite_on_first_metrics = function()
    if not state.suite_running or state._suite_gen ~= my_gen then
      return
    end
    system.set_timeout(function()
      if not state.suite_running then
        return
      end
      M.schedule(state, config, config_ui, next_index)
    end, config.SUITE_PHASE_MS, "ms")
  end

  local fallback_ms = math.max(config.SUITE_PHASE_MS * 4, 45000)
  system.set_timeout(function()
    if not state.suite_running or state._suite_gen ~= my_gen then
      return
    end
    if state._suite_on_first_metrics then
      state._suite_waiting_metrics_for = nil
      state._suite_on_first_metrics = nil
      config_ui.toast("Suite: skipping metrics wait (timeout) → " .. tostring(state.phase), false)
      M.schedule(state, config, config_ui, next_index)
    end
  end, fallback_ms, "ms")
end

function M.schedule(state, config, config_ui, index)
  if not state.suite_running then
    return
  end
  if index > #ORDER then
    state.suite_running = false
    state._suite_arm_after_opencv_ack = nil
    state._suite_waiting_metrics_for = nil
    state._suite_on_first_metrics = nil
    state._suite_gen = (state._suite_gen or 0) + 1
    state.phase = "idle"
    state.opencv_acknowledged = true
    config_ui.toast("Suite finished — see results in the panel.", true)
    config_ui.push_runtime(state)
    return
  end

  state.phase = ORDER[index]
  if state.phase == "opencv" then
    state.opencv_acknowledged = false
  else
    state.opencv_acknowledged = true
  end
  state._suite_arm_after_opencv_ack = nil

  config_ui.toast(
    "Suite: "
      .. state.phase
      .. " (wait for metrics window, then "
      .. string.format("%.1f", config.SUITE_PHASE_MS / 1000)
      .. "s dwell)",
    true
  )
  config_ui.push_runtime(state)

  if state.phase == "opencv" and not state.opencv_acknowledged then
    state._suite_arm_after_opencv_ack = function()
      arm_next_after_metrics_and_dwell(state, config, config_ui, index + 1)
    end
  else
    arm_next_after_metrics_and_dwell(state, config, config_ui, index + 1)
  end
end

function M.start(state, config, config_ui)
  if state.suite_running then
    return
  end
  state._suite_arm_after_opencv_ack = nil
  state._suite_waiting_metrics_for = nil
  state._suite_on_first_metrics = nil
  state._suite_gen = (state._suite_gen or 0) + 1
  state.suite_running = true
  M.schedule(state, config, config_ui, 1)
end

function M.stop(state, config_ui)
  state.suite_running = false
  state._suite_arm_after_opencv_ack = nil
  state._suite_waiting_metrics_for = nil
  state._suite_on_first_metrics = nil
  state._suite_gen = (state._suite_gen or 0) + 1
  config_ui.toast("Suite stopped.", false)
  config_ui.push_runtime(state)
end

return M
