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
    return default
  end
  return v
end

local M = {}

M.TARGET_FPS = u("TARGET_FPS", 60)
M.SUITE_PHASE_MS = u("SUITE_PHASE_MS", 4500)
M.METRICS_WINDOW_FRAMES = u("METRICS_WINDOW_FRAMES", 30)

-- Batched phases (color / input / fs): run as many ops as fit in each capture callback
-- without exceeding a fraction of one frame slot at TARGET_FPS (see workloads.lua).
M.FRAME_BUDGET_FRACTION = u("FRAME_BUDGET_FRACTION", 0.88)
M.ADAPTIVE_MAX_OPS_PER_FRAME = u("ADAPTIVE_MAX_OPS_PER_FRAME", 262144)
M.INPUT_ADAPTIVE_STRIDE = u("INPUT_ADAPTIVE_STRIDE", 32)

-- OpenCV: full-frame scene vs small corner template; one match_template per frame (timed)
M.OPENCV_TEMPLATE_SIZE = u("OPENCV_TEMPLATE_SIZE", 64)

M.FS_BENCH_PATH = u("FS_BENCH_PATH", "config.lua")

-- Legacy layout ratios (unused for color/OCR; kept for manifest compatibility)
M.SCAN_REGION_X_RATIO = u("SCAN_REGION_X_RATIO", 0.2)
M.SCAN_REGION_Y_RATIO = u("SCAN_REGION_Y_RATIO", 0.18)
M.SCAN_REGION_W_RATIO = u("SCAN_REGION_W_RATIO", 0.55)
M.SCAN_REGION_H_RATIO = u("SCAN_REGION_H_RATIO", 0.5)

return M
