local M = {}

function M.round(v)
  return math.floor(v + 0.5)
end

function M.round_rect(rect)
  return {
    x = M.round(rect.x),
    y = M.round(rect.y),
    w = M.round(rect.w),
    h = M.round(rect.h),
  }
end

function M.init(config)
  local w, h = system.screen.get_dimensions()
  local scan_region = {
    x = w * config.SCAN_REGION_X_RATIO,
    y = h * config.SCAN_REGION_Y_RATIO,
    w = w * config.SCAN_REGION_W_RATIO,
    h = h * config.SCAN_REGION_H_RATIO,
  }

  local click_x = M.round(w * config.CLICK_X_RATIO)
  local click_y = M.round(h * config.CLICK_Y_RATIO)

  return {
    screen_w = w,
    screen_h = h,
    capture_region = { x = 0, y = 0, w = w, h = h },
    scan_region = scan_region,
    click_x = click_x,
    click_y = click_y,
  }
end

return M
