local config = require("config")
local config_ui = require("config_ui")
local state = require("state")
local geometry = require("geometry")
local colors = require("color")
local regions = require("regions")
local action_task = require("action_task")
local capture = require("capture")

config_ui.setup(config)

local ctx = regions.init(config, geometry)

system.on_key("ctrl+q", function()
    print("exiting...")
    system.screen.canvas.clear()
    system.screen.stop_capture()
    system.exit()
end)

system.on_key("ctrl+p", function()
    state.paused = not state.paused
    if state.paused then
        system.screen.canvas.text("PAUSED", 50, 125, 0xFF0000FF, { id = "paused" })
    else
        system.screen.canvas.text("RUNNING", 50, 125, 0x00FF00FF, { id = "paused" })
    end
end)

action_task.start(state, config, ctx)
capture.start(state, config, ctx, geometry, colors, regions)
