local M = {}
local win = nil

local function js_string_literal(s)
  return '"' .. tostring(s):gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"):gsub("\r", "") .. '"'
end

local function encode_json_flat(t)
  local parts = { "{" }
  local first = true
  for k, v in pairs(t) do
    local kind = type(v)
    if kind == "number" or kind == "boolean" then
      if not first then
        parts[#parts + 1] = ","
      end
      first = false
      parts[#parts + 1] = string.format("%q:%s", k, tostring(v))
    end
  end
  parts[#parts + 1] = "}"
  return table.concat(parts)
end

local function parse_kv_payload(payload)
  local out = {}
  if type(payload) ~= "string" then
    return out
  end

  for line in payload:gmatch("[^\r\n]+") do
    local k, v = line:match("^([%w_]+)=(.+)$")
    if k and v then
      local n = tonumber(v)
      if n ~= nil then
        out[k] = n
      elseif v == "true" then
        out[k] = true
      elseif v == "false" then
        out[k] = false
      end
    end
  end
  return out
end

local function serialize_user_config(t)
  local keys = {}
  for k in pairs(t) do
    keys[#keys + 1] = k
  end
  table.sort(keys)

  local out = {
    "-- Written by macro_project_demo UI. Restart macro to apply.\n",
    "return {\n",
  }

  for _, key in ipairs(keys) do
    local value = t[key]
    if type(value) == "number" then
      out[#out + 1] = string.format("  %s = %s,\n", key, tostring(value))
    elseif type(value) == "boolean" then
      out[#out + 1] = string.format("  %s = %s,\n", key, value and "true" or "false")
    end
  end

  out[#out + 1] = "}\n"
  return table.concat(out)
end

local function project_config_user_path()
  if not system._project_dir or system._project_dir == "" then
    return nil
  end
  return system._project_dir .. "/config_user.lua"
end

local function toast(message, ok)
  if not win then
    return
  end
  local esc = tostring(message):gsub("\\", "\\\\"):gsub("'", "\\'"):gsub("\n", " ")
  win:run_js(string.format("window.demoToast('%s', %s);", esc, ok and "true" or "false"))
end

function M.push_runtime(state, stats)
  if not win then
    return
  end

  local payload = {
    paused = state.paused,
    automation_enabled = state.automation_enabled,
    target_seen = state.target_seen,
  }

  if type(stats) == "table" then
    for k, v in pairs(stats) do
      payload[k] = v
    end
  end

  win:run_js("window.demoUpdateRuntime(JSON.parse(" .. js_string_literal(encode_json_flat(payload)) .. "));")
end

function M.setup(config, state, ctx)
  system.ui.on("ui_ready", function()
    local payload = {
      TARGET_FPS = config.TARGET_FPS,
      SCAN_TOLERANCE = config.SCAN_TOLERANCE,
      TARGET_R = config.TARGET_R,
      TARGET_G = config.TARGET_G,
      TARGET_B = config.TARGET_B,
      AUTOMATION_INTERVAL_MS = config.AUTOMATION_INTERVAL_MS,
      CLICK_X_RATIO = config.CLICK_X_RATIO,
      CLICK_Y_RATIO = config.CLICK_Y_RATIO,
      paused = state.paused,
      automation_enabled = state.automation_enabled,
      screen_w = ctx.screen_w,
      screen_h = ctx.screen_h,
    }
    win:run_js("window.demoApply(JSON.parse(" .. js_string_literal(encode_json_flat(payload)) .. "));")
  end)

  system.ui.on("config_save", function(payload)
    local path = project_config_user_path()
    if not path then
      toast("No project dir available", false)
      return
    end

    local overrides = parse_kv_payload(payload)
    local f = system.fs.open(path, "w")
    if not f then
      toast("Failed to open config_user.lua", false)
      return
    end

    f:write(serialize_user_config(overrides))
    f:close()
    toast("Saved config_user.lua. Restart macro to apply.", true)
  end)

  system.ui.on("toggle_pause", function()
    state.paused = not state.paused
    M.push_runtime(state)
  end)

  system.ui.on("toggle_automation", function()
    state.automation_enabled = not state.automation_enabled
    M.push_runtime(state)
  end)

  system.ui.on("snapshot", function()
    state.request_snapshot = true
    toast("Snapshot flagged in overlay", true)
  end)

  system.ui.on("ui_target", function(payload)
    local target = parse_kv_payload(payload)
    if target.cx and target.cy then
      state.ui_target_hint = { x = target.cx, y = target.cy }
    end
    if target.scan_x and target.scan_y and target.scan_w and target.scan_h then
      state.ui_scan_region = {
        x = target.scan_x,
        y = target.scan_y,
        w = target.scan_w,
        h = target.scan_h,
      }
    end
  end)

  win = system.ui.open("ui/index.html")
  win:set_title("Machotkey Feature Demo")
  win:set_size(640, 760)
  win:center()
end

return M
