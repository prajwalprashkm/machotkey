local config = require("config")
local state = require("state")
local geometry = require("geometry")
local colors = require("color")
local capture = require("capture")
local automation = require("automation")
local ui = require("ui_controller")

local ctx = geometry.init(config)
ui.setup(config, state, ctx)

automation.start(state, config, ctx)
capture.start(state, config, ctx, colors, ui, geometry)

system.on_key("ctrl+q", function()
  system.screen.canvas.clear()
  system.screen.stop_capture()
  system.exit()
end)

system.on_key("ctrl+p", function()
  state.paused = not state.paused
  ui.push_runtime(state)
end)

system.on_key("ctrl+a", function()
  if state.automation_enabled then
    state.automation_enabled = false
    ui.push_runtime(state)
    return
  end

  -- Demonstrates delayed scheduling with set_timeout.
  system.screen.canvas.text("AUTOMATION ARMS IN 0.3s", 40, 210, 0xF59E0BFF, { id = "arm_msg" })
  system.set_timeout(function()
    state.automation_enabled = true
    system.screen.canvas.text("AUTOMATION ACTIVE", 40, 210, 0x22C55EFF, { id = "arm_msg" })
    ui.push_runtime(state)
  end, 300, "ms")
end)

system.on_key("ctrl+s", function()
  state.request_snapshot = true
end)
