--[[
  Automated walk through benchmark phases (idle → color → OCR → …).
  Each phase runs for config.SUITE_PHASE_MS so capture metrics can stabilize.
  OpenCV: timer for the *next* suite step starts only after the user acknowledges
  the CPU warning in the WebView (opencv_ack).
]]

local M = {}

local ORDER = { "idle", "color", "ocr_fast", "ocr_accurate", "input", "opencv", "fs" }

local function arm_suite_step(state, config, config_ui, next_index)
  if not state.suite_running then
    return
  end
  system.set_timeout(function()
    M.schedule(state, config, config_ui, next_index)
  end, config.SUITE_PHASE_MS, "ms")
end

function M.schedule(state, config, config_ui, index)
  if not state.suite_running then
    return
  end
  if index > #ORDER then
    state.suite_running = false
    state._suite_arm_after_opencv_ack = nil
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

  config_ui.toast("Suite: " .. state.phase .. " (" .. (config.SUITE_PHASE_MS / 1000) .. "s)", true)
  config_ui.push_runtime(state)

  if state.phase == "opencv" and not state.opencv_acknowledged then
    state._suite_arm_after_opencv_ack = function()
      arm_suite_step(state, config, config_ui, index + 1)
    end
  else
    arm_suite_step(state, config, config_ui, index + 1)
  end
end

function M.start(state, config, config_ui)
  if state.suite_running then
    return
  end
  state._suite_arm_after_opencv_ack = nil
  state.suite_running = true
  M.schedule(state, config, config_ui, 1)
end

function M.stop(state, config_ui)
  state.suite_running = false
  state._suite_arm_after_opencv_ack = nil
  config_ui.toast("Suite stopped.", false)
  config_ui.push_runtime(state)
end

return M
