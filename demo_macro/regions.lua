local M = {}

function M.init(config, geometry)
  local w, h = system.screen.get_dimensions()

  local tw = math.max(8, math.min(config.OPENCV_TEMPLATE_SIZE, w - 2, h - 2))
  local th = tw

  local full = { x = 0, y = 0, w = w, h = h }

  return {
    screen_w = w,
    screen_h = h,
    capture_region = geometry.round_rect(full),
    full_frame = geometry.round_rect(full),
    opencv_scene = geometry.round_rect(full),
    opencv_template_rect = geometry.round_rect({ x = 0, y = 0, w = tw, h = th }),
  }
end

return M
