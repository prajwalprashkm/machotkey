local M = {
    current_stage = 0,
    action = -1,
    direction = 0,
    distance_factor = 0,
    side_toggle = true,
    ankle_break = false,
    ankle_break_duration = 0,
    last_cast = 0,
    last_match = nil,
    last_finish = 0,
    last_shake_scan = 0,
    not_found_counter = 0,
    last_fish_x = nil,
    paused = true,

    task_wait_until = 0,
    task_step = 0,
    last_duration = 0,

    n_frames = 0,
    last_frame_running = false,
    dropped_frames = 0,
    dropped_frames_per = 0,
    action_taken = false,
    last_index = 0,
    last_stop_n_frames = 0,
    start_time = 0,
}

return M
