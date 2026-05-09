local M = {}

function M.init(config, geometry)
    local w, h = system.screen.get_dimensions()
    local screen_region = { x = 0, y = 0, w = w, h = h }

    local camera_check_region = { x = w / 2.8444, y = h / 1.28, w = (w / 1.5421) - (w / 2.8444), h = h - (h / 1.28) }
    local click_shake_region = { x = w / 4, y = h / 8, w = (w / 1.2736) - (w / 4), h = (h / 1.3409) - (h / 8) }
    local fish_bar_region = { x = w / 3.3160, y = h / 1.1764, w = (w / 1.4317) - (w / 3.3160), h = (h / 1.1512) - (h / 1.1764) }
    local progress_area_region = { x = w / 2.55, y = h / 1.13, w = (w / 1.63) - (w / 2.55), h = (h / 1.08) - (h / 1.13) }

    local capture_region = geometry.union(camera_check_region, click_shake_region, fish_bar_region, progress_area_region)
    camera_check_region = geometry.get_corrected_rect(camera_check_region, screen_region, capture_region)
    click_shake_region = geometry.get_corrected_rect(click_shake_region, screen_region, capture_region)
    fish_bar_region = geometry.get_corrected_rect(fish_bar_region, screen_region, capture_region)
    progress_area_region = geometry.get_corrected_rect(progress_area_region, screen_region, capture_region)

    local ctx = {
        w = w,
        h = h,
        camera_check_region = camera_check_region,
        click_shake_region = click_shake_region,
        fish_bar_region = fish_bar_region,
        progress_area_region = progress_area_region,
        capture_region = capture_region,
        look_down_x = w / 2,
        look_down_y = h / 4,
        pixel_scaling = 1034 / fish_bar_region.w,
    }

    ctx.white_bar_size = math.floor(0.40350877193 * w * (0.3 + config.CONTROL) + 0.5)
    M.recalculate(ctx, config)
    return ctx
end

function M.recalculate(ctx, config)
    ctx.max_left_bar = (ctx.w / 3.3160) + ctx.white_bar_size * config.SIDE_BAR_RATIO - ctx.capture_region.x
    ctx.max_right_bar = (ctx.w / 1.4317) - ctx.white_bar_size * config.SIDE_BAR_RATIO - ctx.capture_region.x
    ctx.deadzone = 0.01 * ctx.white_bar_size
    ctx.deadzone2 = 0.1 * ctx.white_bar_size
end

return M
