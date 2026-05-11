--[[
  Color / input / fs: batched ops → average µs/op.
  OCR + OpenCV: heavy — one task per frame, timing = that single task (ops always 1).
]]

local M = {}

local function set_stats(state, ops, total_us)
  if not ops or ops <= 0 then
    state.workload_ops = 0
    state.workload_total_us = 0
    state.workload_us_per_op = 0
    return
  end
  state.workload_ops = ops
  state.workload_total_us = total_us
  state.workload_us_per_op = total_us / ops
end

local function color_fullframe(config, ctx, state)
  local n = config.COLOR_SEARCHES_PER_FRAME
  local rect = ctx.full_frame
  local t0 = system.get_time("us")
  for i = 1, n do
    local k = state.color_seed + i * 17
    local r = k % 256
    local g = (k * 3 + 90) % 256
    local b = (k * 5 + 180) % 256
    local tol = (k % 24) + 2
    system.screen.find_color(r, g, b, tol, rect)
  end
  local t1 = system.get_time("us")
  state.color_seed = (state.color_seed + n * 17) % 65536
  set_stats(state, n, t1 - t0)
end

local function ocr_fullframe(fast, _config, state)
  local fn = fast and system.screen.ocr.fast.recognize_text or system.screen.ocr.accurate.recognize_text
  local t0 = system.get_time("us")
  pcall(function()
    fn({})
  end)
  local t1 = system.get_time("us")
  set_stats(state, 1, t1 - t0)
end

local function input_batch(config, state)
  local n = config.INPUT_GETPOSITION_BATCH
  n = math.max(1, math.floor(n))
  local t0 = system.get_time("us")
  for _ = 1, n do
    system.mouse.get_position()
  end
  local t1 = system.get_time("us")
  set_stats(state, n, t1 - t0)
end

local function opencv_once(_config, ctx, state)
  local buf = system.screen.buffer
  if not buf or not buf.crop then
    set_stats(state, 0, 0)
    return
  end
  local t0 = system.get_time("us")
  local scene_roi = buf:crop(ctx.opencv_scene)
  local tpl_roi = buf:crop(ctx.opencv_template_rect)
  if not scene_roi or not tpl_roi then
    set_stats(state, 0, 0)
    return
  end
  local scene_mat = scene_roi:to_opencv_mat({ copy = true })
  local tpl_mat = tpl_roi:to_opencv_mat({ copy = true })
  if not scene_mat or not tpl_mat then
    set_stats(state, 0, 0)
    return
  end
  scene_mat:match_template(tpl_mat, { raw = true, threshold = 0.35 })
  local t1 = system.get_time("us")
  set_stats(state, 1, t1 - t0)
end

local function fs_batch(config, state)
  local path = config.FS_BENCH_PATH
  local n = math.max(1, math.floor(config.FS_READS_PER_FRAME))
  local t0 = system.get_time("us")
  for _ = 1, n do
    system.fs.read_all(path)
  end
  local t1 = system.get_time("us")
  set_stats(state, n, t1 - t0)
end

function M.run_phase(state, config, ctx, phase)
  if phase == "idle" then
    set_stats(state, 0, 0)
    return
  elseif phase == "color" then
    color_fullframe(config, ctx, state)
  elseif phase == "ocr_fast" then
    ocr_fullframe(true, config, state)
  elseif phase == "ocr_accurate" then
    ocr_fullframe(false, config, state)
  elseif phase == "input" then
    input_batch(config, state)
  elseif phase == "opencv" then
    if state.opencv_acknowledged then
      opencv_once(config, ctx, state)
    else
      set_stats(state, 0, 0)
    end
  elseif phase == "fs" then
    fs_batch(config, state)
  end
end

return M
