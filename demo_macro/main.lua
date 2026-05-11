local config = require("config")
local config_ui = require("config_ui")
local state = require("state")
local geometry = require("geometry")
local color = require("color")
local regions = require("regions")
local capture = require("capture")

--[[
  Entry layout mirrors macro_project:
  1) WebView + event wiring
  2) regions / layout from screen size
  3) optional host-facing hotkeys
  4) begin_capture last
]]

config_ui.setup(config, state)

local ctx = regions.init(config, geometry)

config_ui.after_layout(state, ctx)

pcall(function()
  system.permissions.get()
end)

system.on_key("ctrl+q", function()
  system.screen.canvas.clear()
  pcall(function()
    system.screen.stop_capture()
  end)
  system.exit()
end)

system.on_key("ctrl+p", function()
  state.paused = not state.paused
  config_ui.push_runtime(state)
end)

capture.start(state, config, ctx, geometry, color, regions)
