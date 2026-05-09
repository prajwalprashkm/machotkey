local U = {}
do
    local ok, t = pcall(require, "config_user")
    if ok and type(t) == "table" then
        U = t
    end
end

local function u(key, default)
    local v = U[key]
    if v == nil then
        print("returning default for "..key)
        return default
    end
    return v
end

local M = {}

M.TARGET_FPS = u("TARGET_FPS", 60)
M.MAX_LATENCY = u("MAX_LATENCY", 5)
M.MAX_CPU = u("MAX_CPU", 50)
M.MAX_RAM = u("MAX_RAM", 2048)

M.CLICK_SHAKE_MODE = u("CLICK_SHAKE_MODE", false)
M.POLARIS_MODE = u("POLARIS_MODE", false)
M.SERA_MODE = u("SERA_MODE", false)
M.REAVER_MODE = u("REAVER_MODE", false)
M.CASTBOUND_MODE = u("CASTBOUND_MODE", false)

M.RESTART_DELAY = u("RESTART_DELAY", 1000)
M.HOLD_ROD_CAST_DURATION = u("HOLD_ROD_CAST_DURATION", 200)
M.WAIT_FOR_BOBBER_DELAY = u("WAIT_FOR_BOBBER_DELAY", 12)
M.BAIT_DELAY = u("BAIT_DELAY", 0)

M.SHAKE_FAILSAFE = u("SHAKE_FAILSAFE", 20000)
M.CLICK_SHAKE_COLOR_TOLERANCE = u("CLICK_SHAKE_COLOR_TOLERANCE", 3)
M.CLICK_SCAN_DELAY = u("CLICK_SCAN_DELAY", 10)

M.CONTROL = u("CONTROL", 0.25)
M.FISH_BAR_COLOR_TOLERANCE = u("FISH_BAR_COLOR_TOLERANCE", 5)
M.WHITE_BAR_COLOR_TOLERANCE = u("WHITE_BAR_COLOR_TOLERANCE", 16)
M.ARROW_COLOR_TOLERANCE = u("ARROW_COLOR_TOLERANCE", 6)
M.SCAN_DELAY = u("SCAN_DELAY", 10)
M.SIDE_BAR_RATIO = u("SIDE_BAR_RATIO", 0.55)
M.SIDE_DELAY = u("SIDE_DELAY", 400)

M.STABLE_RIGHT_MULTIPLIER = u("STABLE_RIGHT_MULTIPLIER", 2)
M.STABLE_RIGHT_DIVISION = u("STABLE_RIGHT_DIVISION", 1.31)
M.STABLE_LEFT_MULTIPLIER = u("STABLE_LEFT_MULTIPLIER", 1.75)
M.STABLE_LEFT_DIVISION = u("STABLE_LEFT_DIVISION", 1.25)

M.UNSTABLE_RIGHT_MULTIPLIER = u("UNSTABLE_RIGHT_MULTIPLIER", 2.25)
M.UNSTABLE_RIGHT_DIVISION = u("UNSTABLE_RIGHT_DIVISION", 1.54)
M.UNSTABLE_LEFT_MULTIPLIER = u("UNSTABLE_LEFT_MULTIPLIER", 2.35)
M.UNSTABLE_LEFT_DIVISION = u("UNSTABLE_LEFT_DIVISION", 1.25)

M.RIGHT_ANKLE_BREAK_MULTIPLIER = u("RIGHT_ANKLE_BREAK_MULTIPLIER", 0.25)
M.LEFT_ANKLE_BREAK_MULTIPLIER = u("LEFT_ANKLE_BREAK_MULTIPLIER", 0.15)

M.NOT_FOUND_THRESHOLD = u("NOT_FOUND_THRESHOLD", M.REAVER_MODE and 25 or 10)

M.SHAKE_COLOR = {
    r = u("SHAKE_R", 255),
    g = u("SHAKE_G", 255),
    b = u("SHAKE_B", 255),
}

if M.POLARIS_MODE then
    M.FISH_COLOR = { r = 98, g = 199, b = 240 }
    M.BAR_COLOR = { r = 255, g = 255, b = 255 }
    M.ARROW_COLOR = { r = 132, g = 133, b = 135 }
elseif M.CASTBOUND_MODE then
    M.FISH_COLOR = { r = 65, g = 21, b = 137 }
    M.BAR_COLOR = { r = 153, g = 224, b = 222 }
    M.ARROW_COLOR = { r = 221, g = 87, b = 223 }
else
    M.FISH_COLOR = { r = 67, g = 75, b = 91 }
    M.BAR_COLOR = { r = 255, g = 255, b = 255 }
    M.ARROW_COLOR = { r = 132, g = 133, b = 135 }
end

if U.FISH_R ~= nil or U.FISH_G ~= nil or U.FISH_B ~= nil then
    M.FISH_COLOR = {
        r = u("FISH_R", M.FISH_COLOR.r),
        g = u("FISH_G", M.FISH_COLOR.g),
        b = u("FISH_B", M.FISH_COLOR.b),
    }
end
if U.BAR_R ~= nil or U.BAR_G ~= nil or U.BAR_B ~= nil then
    M.BAR_COLOR = {
        r = u("BAR_R", M.BAR_COLOR.r),
        g = u("BAR_G", M.BAR_COLOR.g),
        b = u("BAR_B", M.BAR_COLOR.b),
    }
end
if U.ARROW_R ~= nil or U.ARROW_G ~= nil or U.ARROW_B ~= nil then
    M.ARROW_COLOR = {
        r = u("ARROW_R", M.ARROW_COLOR.r),
        g = u("ARROW_G", M.ARROW_COLOR.g),
        b = u("ARROW_B", M.ARROW_COLOR.b),
    }
end

return M
