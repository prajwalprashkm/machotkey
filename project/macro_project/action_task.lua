local M = {}

function M.start(state, config, ctx)
    system._create_task(function()
        while true do
            system.wait(1, "ms")
            local now = system.get_time("ms")

            if state.paused or state.action == -1 then
                state.task_step = 0
                state.task_wait_until = 0
            elseif now >= state.task_wait_until then
                if state.action == 0 then
                    system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.DOWN, ctx.look_down_x, ctx.look_down_y)
                    system.wait(10, "ms")
                    system.mouse.send(system.mouse.Button.LEFT, system.mouse.EventType.UP, ctx.look_down_x, ctx.look_down_y)
                    state.task_wait_until = now + 20
                elseif state.action == 3 or state.action == 4 then
                    if not state.side_toggle then
                        state.ankle_break_duration = 0
                        state.side_toggle = true
                        system.mouse.send(
                            system.mouse.Button.LEFT,
                            (state.action == 3 and system.mouse.EventType.UP or system.mouse.EventType.DOWN),
                            ctx.look_down_x,
                            ctx.look_down_y
                        )
                        state.task_wait_until = now + config.SIDE_DELAY
                    else
                        state.task_wait_until = now + config.SCAN_DELAY
                    end
                else
                    state.side_toggle = false
                    local is_right = (state.action == 2 or state.action == 6)
                    local is_unstable = (state.action == 5 or state.action == 6)

                    if state.task_step == 0 then
                        system.mouse.send(
                            system.mouse.Button.LEFT,
                            is_right and system.mouse.EventType.DOWN or system.mouse.EventType.UP,
                            ctx.look_down_x,
                            ctx.look_down_y
                        )

                        if (is_right and state.ankle_break) or (not is_right and not state.ankle_break) then
                            state.task_wait_until = now + state.ankle_break_duration
                            state.ankle_break_duration = 0
                            state.task_step = 1
                        else
                            state.task_step = 1
                            state.task_wait_until = now
                        end
                    elseif state.task_step == 1 then
                        local adapt
                        if state.distance_factor < 0.2 then
                            adapt = 0.15 + 0.15 * state.distance_factor
                        else
                            adapt = 0.5 + 0.5 * math.pow(state.distance_factor, 1.2)
                        end

                        local mult
                        if is_right then
                            mult = is_unstable and config.UNSTABLE_RIGHT_MULTIPLIER or config.STABLE_RIGHT_MULTIPLIER
                        else
                            mult = is_unstable and config.UNSTABLE_LEFT_MULTIPLIER or config.STABLE_LEFT_MULTIPLIER
                        end

                        local duration = math.abs(state.direction) * mult * ctx.pixel_scaling * adapt
                        if is_unstable then
                            local max_dur
                            if config.CONTROL >= 0.25 then
                                max_dur = ctx.white_bar_size * 0.75
                            else
                                max_dur = ctx.white_bar_size * 0.85
                            end
                            duration = math.max(10, math.min(duration, max_dur))
                        end

                        state.last_duration = duration
                        state.task_wait_until = now + duration
                        state.task_step = 2
                    elseif state.task_step == 2 then
                        system.mouse.send(
                            system.mouse.Button.LEFT,
                            is_right and system.mouse.EventType.UP or system.mouse.EventType.DOWN,
                            ctx.look_down_x,
                            ctx.look_down_y
                        )

                        local div
                        if is_right then
                            div = is_unstable and config.UNSTABLE_RIGHT_DIVISION or config.STABLE_RIGHT_DIVISION
                        else
                            div = is_unstable and config.UNSTABLE_LEFT_DIVISION or config.STABLE_LEFT_DIVISION
                        end
                        local counter = state.last_duration / div

                        state.ankle_break = not is_right
                        if is_right then
                            state.ankle_break_duration = (state.last_duration - counter) * config.RIGHT_ANKLE_BREAK_MULTIPLIER
                        else
                            state.ankle_break_duration = (state.last_duration - counter) * config.LEFT_ANKLE_BREAK_MULTIPLIER
                        end

                        state.task_wait_until = now + counter
                        state.task_step = 0
                    end
                end
            end
        end
    end)
end

return M
