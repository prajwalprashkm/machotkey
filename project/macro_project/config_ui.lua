-- WebView config editor (demo): pushes state to UI and saves config_user.lua for next run.

local win = nil
local M = {}

local function js_string_literal(s)
    return '"' .. s:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"):gsub("\r", "") .. '"'
end

local function json_for_apply(t)
    local parts = { "{" }
    local first = true
    for k, v in pairs(t) do
        if type(v) == "number" then
            if not first then
                parts[#parts + 1] = ","
            end
            first = false
            parts[#parts + 1] = string.format("%q:%s", k, tostring(v))
        elseif type(v) == "boolean" then
            if not first then
                parts[#parts + 1] = ","
            end
            first = false
            parts[#parts + 1] = string.format("%q:%s", k, v and "true" or "false")
        end
    end
    parts[#parts + 1] = "}"
    return table.concat(parts)
end

local function flat_state_from_config(c)
    return {
        TARGET_FPS = c.TARGET_FPS,
        MAX_LATENCY = c.MAX_LATENCY,
        MAX_CPU = c.MAX_CPU,
        MAX_RAM = c.MAX_RAM,
        CLICK_SHAKE_MODE = c.CLICK_SHAKE_MODE,
        POLARIS_MODE = c.POLARIS_MODE,
        SERA_MODE = c.SERA_MODE,
        REAVER_MODE = c.REAVER_MODE,
        CASTBOUND_MODE = c.CASTBOUND_MODE,
        RESTART_DELAY = c.RESTART_DELAY,
        HOLD_ROD_CAST_DURATION = c.HOLD_ROD_CAST_DURATION,
        WAIT_FOR_BOBBER_DELAY = c.WAIT_FOR_BOBBER_DELAY,
        BAIT_DELAY = c.BAIT_DELAY,
        SHAKE_FAILSAFE = c.SHAKE_FAILSAFE,
        CLICK_SHAKE_COLOR_TOLERANCE = c.CLICK_SHAKE_COLOR_TOLERANCE,
        CLICK_SCAN_DELAY = c.CLICK_SCAN_DELAY,
        CONTROL = c.CONTROL,
        FISH_BAR_COLOR_TOLERANCE = c.FISH_BAR_COLOR_TOLERANCE,
        WHITE_BAR_COLOR_TOLERANCE = c.WHITE_BAR_COLOR_TOLERANCE,
        ARROW_COLOR_TOLERANCE = c.ARROW_COLOR_TOLERANCE,
        SCAN_DELAY = c.SCAN_DELAY,
        SIDE_BAR_RATIO = c.SIDE_BAR_RATIO,
        SIDE_DELAY = c.SIDE_DELAY,
        STABLE_RIGHT_MULTIPLIER = c.STABLE_RIGHT_MULTIPLIER,
        STABLE_RIGHT_DIVISION = c.STABLE_RIGHT_DIVISION,
        STABLE_LEFT_MULTIPLIER = c.STABLE_LEFT_MULTIPLIER,
        STABLE_LEFT_DIVISION = c.STABLE_LEFT_DIVISION,
        UNSTABLE_RIGHT_MULTIPLIER = c.UNSTABLE_RIGHT_MULTIPLIER,
        UNSTABLE_RIGHT_DIVISION = c.UNSTABLE_RIGHT_DIVISION,
        UNSTABLE_LEFT_MULTIPLIER = c.UNSTABLE_LEFT_MULTIPLIER,
        UNSTABLE_LEFT_DIVISION = c.UNSTABLE_LEFT_DIVISION,
        RIGHT_ANKLE_BREAK_MULTIPLIER = c.RIGHT_ANKLE_BREAK_MULTIPLIER,
        LEFT_ANKLE_BREAK_MULTIPLIER = c.LEFT_ANKLE_BREAK_MULTIPLIER,
        NOT_FOUND_THRESHOLD = c.NOT_FOUND_THRESHOLD,
        SHAKE_R = c.SHAKE_COLOR.r,
        SHAKE_G = c.SHAKE_COLOR.g,
        SHAKE_B = c.SHAKE_COLOR.b,
        FISH_R = c.FISH_COLOR.r,
        FISH_G = c.FISH_COLOR.g,
        FISH_B = c.FISH_COLOR.b,
        BAR_R = c.BAR_COLOR.r,
        BAR_G = c.BAR_COLOR.g,
        BAR_B = c.BAR_COLOR.b,
        ARROW_R = c.ARROW_COLOR.r,
        ARROW_G = c.ARROW_COLOR.g,
        ARROW_B = c.ARROW_COLOR.b,
    }
end

local function parse_kv_payload(s)
    local t = {}
    if not s or s == "" then
        return t
    end
    for line in s:gmatch("[^\r\n]+") do
        local k, v = line:match("^([%w_]+)=(.+)$")
        if k and v then
            if v == "true" then
                t[k] = true
            elseif v == "false" then
                t[k] = false
            else
                local n = tonumber(v)
                if n then
                    t[k] = n
                end
            end
        end
    end
    return t
end

local function serialize_user_lua(t)
    local keys = {}
    for k in pairs(t) do
        keys[#keys + 1] = k
    end
    table.sort(keys)
    local out = {
        "-- Written by Machotkey config UI (demo). Restart the macro to apply.\n",
        "return {\n",
    }
    for _, k in ipairs(keys) do
        local v = t[k]
        if type(v) == "boolean" then
            out[#out + 1] = string.format("    %s = %s,\n", k, v and "true" or "false")
        elseif type(v) == "number" then
            out[#out + 1] = string.format("    %s = %s,\n", k, tostring(v))
        end
    end
    out[#out + 1] = "}\n"
    return table.concat(out)
end

local function user_path()
    print(system._project_dir)
    local root = system._project_dir
    if not root or root == "" then
        return nil
    end
    return root .. "/config_user.lua"
end

local function toast(msg, ok)
    local esc = tostring(msg):gsub("\\", "\\\\"):gsub("'", "\\'"):gsub("\n", " ")
    win:run_js(string.format("window.macroConfigToast('%s',%s);", esc, ok and "true" or "false"))
end

function M.setup(config)
    system.ui.on("ui_ready", function()
        local json = json_for_apply(flat_state_from_config(config))
        local js = "window.macroConfigApply(JSON.parse(" .. js_string_literal(json) .. "));"
        win:run_js(js)
    end)

    system.ui.on("config_save", function(payload)
        local t = parse_kv_payload(payload)
        local path = user_path()
        if not path then
            toast("No project dir", false)
            return
        end
        --[[local f, err = io.open(path, "w")
        if not f then
            toast("Save failed: " .. tostring(err), false)
            return
        end
        f:write(serialize_user_lua(t))
        f:close()]]
        local f = system.fs.open(path, "w")
        if not f then
            toast("Save failed!", false)
            return
        end
        
        if t.POLARIS_MODE then
            t.FISH_R = 98
            t.FISH_G = 199
            t.FISH_B = 240
            t.BAR_R = 255
            t.BAR_G = 255
            t.BAR_B = 255
            t.ARROW_R = 132
            t.ARROW_G = 133
            t.ARROW_B = 135
        elseif t.CASTBOUND_MODE then
            t.FISH_R = 65
            t.FISH_G = 21
            t.FISH_B = 137
            t.BAR_R = 153
            t.BAR_G = 224
            t.BAR_B = 222
            t.ARROW_R = 221
            t.ARROW_G = 87
            t.ARROW_B = 223
        end
        f:write(serialize_user_lua(t))
        f:close()
        toast("Saved config_user.lua - restart macro to apply.", true)
    end)

    system.ui.on("config_reset", function()
        local path = user_path()
        if not path then
            toast("No project dir", false)
            return
        end
        --[[local f, err = io.open(path, "w")
        if not f then
            toast("Reset failed: " .. tostring(err), false)
            return
        end
        f:write("return {}\n")
        f:close()
        f:close()]]
        local f = system.fs.open(path, "w")
        if not f then
            toast("Reset failed!", false)
            return
        end
        f:write("return {}\n")
        f:close()
        toast("Reset overrides - restart macro for defaults.", true)
    end)

    win = system.ui.open("ui/index.html")
    win:set_title("Macro config")
    win:set_size(600,800)
    win:center()

    --win:show()
end

return M
