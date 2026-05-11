-- WebView bridge (same usage style as macro_project.config_ui: open window, ui.on, fs for overrides).

local win = nil
local M = {}

local suite = require("suite")

local function js_string_literal(s)
  return '"' .. tostring(s):gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"):gsub("\r", "") .. '"'
end

local function json_finite_number(v)
  if type(v) ~= "number" then
    return nil
  end
  if v ~= v or v == math.huge or v == -math.huge then
    return nil
  end
  return v
end

local function json_for_ui(t)
  local parts = { "{" }
  local first = true
  for k, v in pairs(t) do
    if not first then
      parts[#parts + 1] = ","
    end
    first = false
    local vs
    if type(v) == "number" then
      local n = json_finite_number(v)
      vs = n and tostring(n) or "null"
    elseif type(v) == "boolean" then
      vs = v and "true" or "false"
    elseif type(v) == "string" then
      vs = string.format("%q", v)
    else
      vs = "null"
    end
    parts[#parts + 1] = string.format("%q:%s", k, vs)
  end
  parts[#parts + 1] = "}"
  return table.concat(parts)
end

local function encode_results_table(results)
  local parts = { "{" }
  local first = true
  for phase, row in pairs(results) do
    if type(row) == "table" and type(phase) == "string" then
      if not first then
        parts[#parts + 1] = ","
      end
      first = false
      parts[#parts + 1] = string.format("%q:{", phase)
      local inner_first = true
      for rk, rv in pairs(row) do
        local fragment = nil
        if type(rv) == "number" then
          local n = json_finite_number(rv)
          if n ~= nil then
            fragment = string.format("%q:%s", rk, tostring(n))
          end
        elseif type(rv) == "string" then
          fragment = string.format("%q:%q", rk, rv)
        elseif type(rv) == "boolean" then
          fragment = string.format("%q:%s", rk, rv and "true" or "false")
        end
        if fragment then
          if not inner_first then
            parts[#parts + 1] = ","
          end
          inner_first = false
          parts[#parts + 1] = fragment
        end
      end
      parts[#parts + 1] = "}"
    end
  end
  parts[#parts + 1] = "}"
  return table.concat(parts)
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
        else
          t[k] = v
        end
      end
    end
  end
  return t
end

local function serialize_user_lua(tbl)
  local keys = {}
  for k in pairs(tbl) do
    keys[#keys + 1] = k
  end
  table.sort(keys)
  local out = {
    "-- Written by demo macro UI. Restart to apply.\n",
    "return {\n",
  }
  for _, k in ipairs(keys) do
    local v = tbl[k]
    if type(v) == "boolean" then
      out[#out + 1] = string.format("  %s = %s,\n", k, v and "true" or "false")
    elseif type(v) == "number" then
      out[#out + 1] = string.format("  %s = %s,\n", k, tostring(v))
    elseif type(v) == "string" then
      out[#out + 1] = string.format("  %q = %q,\n", k, v)
    end
  end
  out[#out + 1] = "}\n"
  return table.concat(out)
end

local function user_config_path()
  local root = system._project_dir
  if not root or root == "" then
    return nil
  end
  return root .. "/config_user.lua"
end

local function toast(msg, ok)
  if not win then
    return
  end
  local esc = tostring(msg):gsub("\\", "\\\\"):gsub("'", "\\'"):gsub("\n", " ")
  win:run_js(string.format("window.demoToast('%s',%s);", esc, ok and "true" or "false"))
end

function M.toast(msg, ok)
  toast(msg, ok)
end

function M.push_runtime(state)
  if not win then
    return
  end
  local runner_cpu, runner_ram_mb, runner_vmem_mb = nil, nil, nil
  if system.stats and system.stats.get_info then
    local st = system.stats.get_info("mb")
    if type(st) == "table" then
      runner_cpu = tonumber(st.cpu)
      runner_ram_mb = tonumber(st.ram)
      runner_vmem_mb = tonumber(st.vmem)
    end
  end
  local payload = {
    phase = state.phase,
    opencv_acknowledged = state.opencv_acknowledged,
    fps = state.fps,
    latency_ms = state.latency_ms,
    raw_fps = state.raw_fps,
    dropped = state.dropped_frames,
    paused = state.paused,
    suite_running = state.suite_running,
    workload_ops = state.workload_ops,
    workload_us_per_op = state.workload_us_per_op,
    workload_total_ms = (state.workload_total_us or 0) / 1000,
    workload_action_label = state.workload_action_label,
    workload_action_hz = state.workload_action_hz,
    workload_action_avg_us_per_op = state.workload_action_avg_us_per_op,
    runner_cpu_percent = runner_cpu,
    runner_ram_mb = runner_ram_mb,
    runner_vmem_mb = runner_vmem_mb,
  }
  win:run_js(
    "window.demoApplyRuntime(JSON.parse(" .. js_string_literal(json_for_ui(payload)) .. "));"
      .. "window.demoApplyResults("
      .. encode_results_table(state.bench_results)
      .. ");"
  )
end

function M.after_layout(state, ctx)
  if not win then
    return
  end
  local patch = {
    screen_w = ctx.screen_w,
    screen_h = ctx.screen_h,
  }
  win:run_js("window.demoApplyLayout(JSON.parse(" .. js_string_literal(json_for_ui(patch)) .. "));")
end

function M.setup(config, state)
  system.ui.on("ui_ready", function()
    local patch = {
      TARGET_FPS = config.TARGET_FPS,
      SUITE_PHASE_MS = config.SUITE_PHASE_MS,
      METRICS_WINDOW_FRAMES = config.METRICS_WINDOW_FRAMES,
      COLOR_SEARCHES_PER_FRAME = config.COLOR_SEARCHES_PER_FRAME,
      OPENCV_TEMPLATE_SIZE = config.OPENCV_TEMPLATE_SIZE,
      INPUT_GETPOSITION_BATCH = config.INPUT_GETPOSITION_BATCH,
      FS_READS_PER_FRAME = config.FS_READS_PER_FRAME,
    }
    win:run_js("window.demoApplyConfig(JSON.parse(" .. js_string_literal(json_for_ui(patch)) .. "));")
    M.push_runtime(state)
  end)

  local function complete_opencv_ack()
    if state.phase ~= "opencv" then
      return
    end
    state.opencv_acknowledged = true
    local arm = state._suite_arm_after_opencv_ack
    state._suite_arm_after_opencv_ack = nil
    if type(arm) == "function" then
      arm()
    end
    M.push_runtime(state)
  end

  system.ui.on("set_phase", function(payload)
    local t = parse_kv_payload(payload)
    local p = t.phase
    if type(p) == "string" and p ~= "" then
      state.phase = p
      state.suite_running = false
      state._suite_arm_after_opencv_ack = nil
      state._suite_waiting_metrics_for = nil
      state._suite_on_first_metrics = nil
      state._suite_gen = (state._suite_gen or 0) + 1
      if p == "opencv" then
        state.opencv_acknowledged = false
      else
        state.opencv_acknowledged = true
      end
      toast("Phase: " .. p, true)
      M.push_runtime(state)
    end
  end)

  system.ui.on("opencv_ack", function()
    complete_opencv_ack()
  end)

  system.ui.on("run_suite", function()
    suite.start(state, config, M)
  end)

  system.ui.on("stop_suite", function()
    suite.stop(state, M)
  end)

  system.ui.on("config_save", function(payload)
    local t = parse_kv_payload(payload)
    local path = user_config_path()
    if not path then
      toast("No project dir", false)
      return
    end
    local f = system.fs.open(path, "w")
    if not f then
      toast("Save failed", false)
      return
    end
    f:write(serialize_user_lua(t))
    f:close()
    toast("Saved config_user.lua — restart macro to apply.", true)
  end)

  system.ui.on("config_reset", function()
    local path = user_config_path()
    if not path then
      toast("No project dir", false)
      return
    end
    local f = system.fs.open(path, "w")
    if not f then
      toast("Reset failed", false)
      return
    end
    f:write("return {}\n")
    f:close()
    toast("Reset overrides — restart for defaults.", true)
  end)

  win = system.ui.open("ui/index.html")
  win:set_title("Machotkey demo & benchmark")
  win:set_size(720, 820)
  win:center()
end

return M
