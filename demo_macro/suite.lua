--[[
  Automated walk through benchmark phases (idle → color → OCR → …).
  Each phase runs for config.SUITE_PHASE_MS so capture metrics can stabilize.
]]

local M = {}

local ORDER = { "idle", "color", "ocr_fast", "ocr_accurate", "input", "opencv", "fs" }

local function schedule(state, config, config_ui, index)
  if not state.suite_running then
    return
  end
  if index > #ORDER then
    state.suite_running = false
    state.phase = "idle"
    config_ui.toast("Suite finished — see results in the panel.", true)
    config_ui.push_runtime(state)
    return
  end

  state.phase = ORDER[index]
  config_ui.toast("Suite: " .. state.phase .. " (" .. (config.SUITE_PHASE_MS / 1000) .. "s)", true)
  config_ui.push_runtime(state)

  system.set_timeout(function()
    schedule(state, config, config_ui, index + 1)
  end, config.SUITE_PHASE_MS, "ms")
end

function M.start(state, config, config_ui)
  if state.suite_running then
    return
  end
  state.suite_running = true
  schedule(state, config, config_ui, 1)
end

function M.stop(state, config_ui)
  state.suite_running = false
  config_ui.toast("Suite stopped.", false)
  config_ui.push_runtime(state)
end

return M
