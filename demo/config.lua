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

M.TARGET_FPS = u("TARGET_FPS", 120)
M.MAX_LATENCY_MS = u("MAX_LATENCY_MS", 5)
M.MAX_CPU = u("MAX_CPU", 65)
M.MAX_RAM = u("MAX_RAM", 3072)

M.SCAN_TOLERANCE = u("SCAN_TOLERANCE", 16)
M.TARGET_R = u("TARGET_R", 0)
M.TARGET_G = u("TARGET_G", 0)
M.TARGET_B = u("TARGET_B", 0)

M.SCAN_REGION_X_RATIO = u("SCAN_REGION_X_RATIO", 0.25)
M.SCAN_REGION_Y_RATIO = u("SCAN_REGION_Y_RATIO", 0.22)
M.SCAN_REGION_W_RATIO = u("SCAN_REGION_W_RATIO", 0.50)
M.SCAN_REGION_H_RATIO = u("SCAN_REGION_H_RATIO", 0.40)

M.CLICK_X_RATIO = u("CLICK_X_RATIO", 0.50)
M.CLICK_Y_RATIO = u("CLICK_Y_RATIO", 0.50)
M.AUTOMATION_INTERVAL_MS = u("AUTOMATION_INTERVAL_MS", 45)
M.AUTOMATION_KEY = u("AUTOMATION_KEY", "space")

return M
