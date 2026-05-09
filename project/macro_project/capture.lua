local M = {}

function M.start(state, config, ctx, geometry, colors, regions)
    system.screen.canvas.rect({ x = 45, y = 45, w = 175, h = 150 }, 0x33333399, { fill = 0x33333399, id = "fps_bg" })
    system.screen.canvas.text("FPS: ----", 50, 50, 0xFFFFFFFF, { id = "fps" })
    system.screen.canvas.text("Dropped frames: ----", 50, 75, 0xFFFFFFFF, { id = "dropped" })
    system.screen.canvas.text("Latency: ----", 50, 100, 0xFFFFFFFF, { id = "latency" })
    system.screen.canvas.text("Press ^P to Start...", 50, 125, 0xFFFFFFFF, { id = "paused" })
    system.screen.canvas.text("CPU Usage: ---", 50, 150, 0xFFFFFFFF, { id = "cpu" })
    system.screen.canvas.text("RAM: ----", 50, 175, 0xFFFFFFFF, { id = "ram" })

    state.start_time = system.get_time("us")
    state.n_frames = 0
    state.last_frame_running = false
    state.dropped_frames = 0
    state.dropped_frames_per = 0
    state.action_taken = false
    state.last_index = 0
    state.last_stop_n_frames = 0

    print(config.FISH_COLOR.r, config.FISH_COLOR.g, config.FISH_COLOR.b, config.CASTBOUND_MODE)

    system.screen.begin_capture(function()
        if state.last_frame_running then
            state.dropped_frames = state.dropped_frames + 1
            return
        end
        state.last_frame_running = true

        state.n_frames = state.n_frames + 1
        local delta = system.screen.index - state.last_index - 1
        state.dropped_frames = state.dropped_frames + delta
        state.dropped_frames_per = state.dropped_frames_per + delta
        state.last_index = system.screen.index

        if system.screen.index % 30 == 0 then
            local ts = system.screen.get_current_timestamp()
            local stop_time = system.get_time("us")
            local dt = stop_time - state.start_time
            local fps = (state.n_frames - state.last_stop_n_frames) * 1000000 / dt
            local effective_fps = (state.n_frames + state.dropped_frames_per - state.last_stop_n_frames) * 1000000 / dt

            state.start_time = stop_time
            state.last_stop_n_frames = state.n_frames
            state.dropped_frames_per = 0

            local usage = system.stats.get_info("mb")

            local factor = math.min(config.TARGET_FPS, fps) / config.TARGET_FPS
            local fps_color = colors.lerpRGBA(0xFF0000FF, 0x00FF00FF, factor)

            local latency = (ts - system.screen.timestamp) / 1000
            factor = math.min(1, latency / config.MAX_LATENCY)
            local latency_color = colors.lerpRGBA(0x00FF00FF, 0xFF0000FF, factor)

            factor = math.min(1, usage.cpu / config.MAX_CPU)
            local cpu_color = colors.lerpRGBA(0x00FF00FF, 0xFF0000FF, factor)

            factor = math.min(1, usage.ram / config.MAX_RAM)
            local ram_color = colors.lerpRGBA(0x00FF00FF, 0xFF0000FF, factor)

            system.screen.canvas.text(string.format("FPS: %.2f (raw %.2f)", fps, effective_fps), 50, 50, fps_color, { id = "fps" })
            system.screen.canvas.text(string.format("Dropped frames: %d", state.dropped_frames), 50, 75, 0xFFFFFFFF, { id = "dropped" })
            system.screen.canvas.text(string.format("Latency: %.2f", latency), 50, 100, latency_color, { id = "latency" })
            system.screen.canvas.text(string.format("CPU Usage: %.2f%%", usage.cpu), 50, 150, cpu_color, { id = "cpu" })
            system.screen.canvas.text(string.format("RAM: %.2f MB", usage.ram), 50, 175, ram_color, { id = "ram" })
        end

        if system.screen.index % 1000 == 0 then
            collectgarbage("step", 100)
        end

        if state.paused then
            state.last_frame_running = false
            return
        end

        if state.current_stage == 0 then
            state.action = -1
            state.current_stage = -1

            system.set_timeout(function()
                system.keyboard.press("2")
                system.wait(50, "ms")
                system.keyboard.press("1")
                system.wait(50, "ms")
                system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.DOWN, ctx.look_down_x, ctx.look_down_y)
                system.wait(config.HOLD_ROD_CAST_DURATION, "ms")
                system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.UP, ctx.look_down_x, ctx.look_down_y)
                system.wait(config.WAIT_FOR_BOBBER_DELAY, "ms")

                state.last_cast = system.get_time("ms")
                state.current_stage = 1
            end, config.RESTART_DELAY, "ms")
        elseif state.current_stage == 1 then
            if system.get_time("ms") - state.last_shake_scan < config.CLICK_SCAN_DELAY then
                state.last_frame_running = false
                return
            end

            state.last_shake_scan = system.get_time("ms")
            local match = system.screen.find_color(config.FISH_COLOR.r, config.FISH_COLOR.g, config.FISH_COLOR.b, config.FISH_BAR_COLOR_TOLERANCE, ctx.fish_bar_region)
            if match then
                state.current_stage = -1
                state.last_match = nil
                system.set_timeout(function()
                    state.current_stage = 2
                end, config.BAIT_DELAY, "ms")
            else
                if system.get_time("ms") - state.last_cast > config.SHAKE_FAILSAFE then
                    state.current_stage = 0
                end
                match = system.screen.find_color(config.SHAKE_COLOR.r, config.SHAKE_COLOR.g, config.SHAKE_COLOR.b, config.CLICK_SHAKE_COLOR_TOLERANCE, ctx.click_shake_region)
                if config.CLICK_SHAKE_MODE then
                    if match and (state.last_match == nil or (math.abs(match.x - state.last_match.x) >= 3 and math.abs(match.y - state.last_match.y) >= 3)) then
                        system.mouse.click(system.mouse.Button.LEFT, match.x + ctx.capture_region.x, match.y + ctx.capture_region.y)
                        state.last_match = match
                    end
                else
                    if match and match ~= state.last_match then
                        system.keyboard.press("enter")
                        state.last_match = match
                    elseif system.screen.index % 60 == 0 then
                        system.keyboard.press("enter")
                    end
                end
            end
        elseif state.current_stage == 2 then
            local fish = system.screen.find_color(config.FISH_COLOR.r, config.FISH_COLOR.g, config.FISH_COLOR.b, config.FISH_BAR_COLOR_TOLERANCE, ctx.fish_bar_region)

            if not fish then
                state.not_found_counter = state.not_found_counter + 1
                if state.not_found_counter > config.NOT_FOUND_THRESHOLD then
                    state.current_stage = 0
                    state.action = -1
                    state.not_found_counter = 0
                    system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.UP, ctx.look_down_x, ctx.look_down_y)
                    state.last_finish = system.get_time("ms")
                    state.action_taken = false
                end
            else
                state.not_found_counter = 0
                local predicted_fish_x = fish.x
                if state.last_fish_x ~= nil then
                    predicted_fish_x = fish.x + (fish.x - state.last_fish_x) * 3
                end
                state.last_fish_x = fish.x

                local bar = system.screen.find_color(config.BAR_COLOR.r, config.BAR_COLOR.g, config.BAR_COLOR.b, config.WHITE_BAR_COLOR_TOLERANCE, ctx.fish_bar_region)
                if bar then
                    local bar_right = system.screen.find_color(config.BAR_COLOR.r, config.BAR_COLOR.g, config.BAR_COLOR.b, config.WHITE_BAR_COLOR_TOLERANCE, ctx.fish_bar_region, true)
                    if bar_right then
                        ctx.white_bar_size = bar_right.x - bar.x
                        regions.recalculate(ctx, config)
                    end
                end

                if fish.x < ctx.max_left_bar then
                    state.action = 3
                elseif fish.x > ctx.max_right_bar then
                    state.action = 4
                else
                    if bar then
                        if config.SERA_MODE and not state.action_taken then
                            if fish.x < bar.x + ctx.white_bar_size / 4 then
                                state.action_taken = true
                                state.action = 3
                                state.last_frame_running = false
                                return
                            elseif fish.x > bar.x + 3 * ctx.white_bar_size / 4 then
                                state.action_taken = true
                                state.action = 4
                                state.last_frame_running = false
                                return
                            end
                        end

                        local bar_center = bar.x + (ctx.white_bar_size / 2)
                        state.direction = bar_center - predicted_fish_x
                        state.distance_factor = math.abs(state.direction) * 2 / ctx.white_bar_size

                        if state.direction > ctx.deadzone and state.direction < ctx.deadzone2 then
                            state.action = 1
                        elseif state.direction < -ctx.deadzone and state.direction > -ctx.deadzone2 then
                            state.action = 2
                        elseif state.direction > ctx.deadzone2 then
                            state.action = 5
                        elseif state.direction < -ctx.deadzone2 then
                            state.action = 6
                        else
                            state.action = 0
                        end
                    else
                        local arrow = system.screen.find_color(config.ARROW_COLOR.r, config.ARROW_COLOR.g, config.ARROW_COLOR.b, config.ARROW_COLOR_TOLERANCE, ctx.fish_bar_region)
                        if arrow then
                            if arrow.x - predicted_fish_x > 0 then
                                state.action = 5
                            else
                                state.action = 6
                            end
                        end
                    end
                end
            end
        end

        state.last_frame_running = false
    end, { fps = config.TARGET_FPS, region = geometry.round_rect(ctx.capture_region) })
end

return M
