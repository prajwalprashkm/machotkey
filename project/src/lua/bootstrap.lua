--[[
    Copyright (c) Prajwal Prashanth. All rights reserved.

    This source code is licensed under the source-available license 
    found in the LICENSE file in the root directory of this source tree.
--]]

local ffi = require("ffi")

-- 1. FFI Definitions: Mapping C++ memory structures to Lua
ffi.cdef[[
    typedef struct {
        uint8_t* data;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
    } PixelBuffer;

    typedef struct {
        const uint8_t* data;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
    } PixelBufferReadOnly;

    struct Color {
        uint8_t b, g, r, a;
    };
    struct SharedBufferHeader {
        uint64_t frame_index;
        uint32_t lock_flag;
        double capture_timestamp;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t data_size;
    } __attribute__((packed));
    struct RunnerState {
        struct SharedBufferHeader* shm_header;
        uint8_t* pixel_data;
        PixelBufferReadOnly buffer;
    } __attribute__((packed));

    typedef struct {
        char _text[128];
        float confidence;
        double x, y, w, h;
    } LuaOCRResult;
    
    typedef struct {
        LuaOCRResult* data;
        size_t size;
    } OCRVector;

    typedef struct {
        void* mat_ptr;
        size_t width, height, stride, channels;
        bool readonly;
    } OpenCVMat;

    typedef struct {
        int x;
        int y;
        double score;
    } OpenCVTemplateMatch;

    typedef struct {
        OpenCVTemplateMatch* data;
        size_t size;
    } OpenCVTemplateMatchVector;

    typedef struct { size_t x, y, width, height; } BoundingRect;

    typedef struct { bool found; int x, y; } ColorMatch;

    PixelBuffer copy_buffer(PixelBuffer src);
    PixelBuffer crop_buffer(PixelBuffer src, uint32_t x, uint32_t y, uint32_t cw, uint32_t ch);
    void free_buffer(PixelBuffer buffer);
    bool _save_frame_ppm(const uint8_t* data, size_t width, size_t height, size_t bytesPerRow, const char* filename);
    bool _save_frame_image(const uint8_t* data, size_t width, size_t height, size_t bytesPerRow, const char* filename);

    ColorMatch find_exact_color_ffi(const uint8_t* data, size_t w, size_t h, size_t stride, BoundingRect rect, uint8_t r, uint8_t g, uint8_t b, bool reverse, bool reverse_vertical);
    ColorMatch find_fuzzy_color_ffi(const uint8_t* data, size_t w, size_t h, size_t stride, BoundingRect rect, uint8_t r, uint8_t g, uint8_t b, uint8_t tol, bool reverse, bool reverse_vertical);
    OCRVector _recognize_text(bool fast, uint8_t* buffer, uint32_t width, uint32_t height, uint32_t stride, bool using_region, size_t x, size_t y, size_t w, size_t h);
    void _free_OCRVector(OCRVector vector);

    OpenCVMat _OpenCVMat_bgra_to_bgr(OpenCVMat mat);
    OpenCVMat _OpenCVMat_bgr_to_bgra(OpenCVMat mat);
    OpenCVMat _PixelBuffer_to_OpenCVMat(bool readonly, PixelBuffer buffer);
    PixelBuffer _OpenCVMat_to_PixelBuffer(OpenCVMat mat);
    OpenCVMat _OpenCVMat_resize(OpenCVMat mat, size_t w, size_t h);
    OpenCVMat _OpenCVMat_load_from_file(const char* filename);
    OpenCVTemplateMatchVector _OpenCVMat_match_template(OpenCVMat scene_m, OpenCVMat tpl_m, double threshold, OpenCVMat mask);
    OpenCVTemplateMatchVector _OpenCVMat_match_template_pyr(OpenCVMat scene_m, OpenCVMat tpl_m, double threshold, OpenCVMat mask_m, OpenCVMat scene_pyr, OpenCVMat tpl_pyr, OpenCVMat mask_pyr);
    OpenCVMat _OpenCVMat_downsample(OpenCVMat mat);
    OpenCVMat _OpenCVMat_upsample(OpenCVMat mat);
    OpenCVMat _OpenCVMat_extract_channel(OpenCVMat mat, size_t channel);
    void _OpenCVMat_free(OpenCVMat mat);
    void _OpenCVTemplateMatchVector_free(OpenCVTemplateMatchVector vec);
]]

-- Helper to validate the region table
local function validate_region(r, source_w, source_h)
    if type(r) ~= "table" then
        error("Crop requires a table containing {x, y, w, h}", 3)
    end
    
    local x, y, w, h = r.x, r.y, r.w or r.width, r.h or r.height
    
    if not (x and y and w and h) then
        error("Crop table missing required keys: x, y, w, h", 3)
    end
    
    if type(x) ~= "number" or type(y) ~= "number" or type(w) ~= "number" or type(h) ~= "number" then
        error("Crop values must be numbers", 3)
    end

    -- Prevent negative values which would wrap around in C++ uint32_t
    if x < 0 or y < 0 or w <= 0 or h <= 0 then
        error("Crop dimensions must be positive", 3)
    end

    -- Optional: Logic to warn if cropping entirely outside the source
    if x >= source_w or y >= source_h then
        error(string.format("Crop origin (%d, %d) is outside source bounds (%dx%d)", x, y, source_w, source_h), 3)
    end

    return x, y, w, h
end

-- Use tonumber() to ensure we have a clean Lua number from the C++ uintptr_t
local raw_addr = tonumber(system._state_addr)
system._state_addr = nil

-- Cast that number to a pointer that LuaJIT FFI understands
local state = ffi.cast("struct RunnerState*", raw_addr)
local project_root = system._project_dir or ""

-- Helper to create a BoundingRect on the stack
local function make_rect(x, y, w, h, max_w, max_h)
    return ffi.new("BoundingRect", x or 0, y or 0, w or max_w or system.screen.width, h or max_h or system.screen.height)
end

local function _find_color(r, g, b, tolerance, rect_table, reverse, reverse_vertical, buffer, width, height, stride)
    if buffer == ffi.NULL then return nil end

    -- Handle the "Overload" logic:
    -- if the 4th argument is a table, the user skipped 'tolerance'
    -- e.g., find_color(255, 0, 0, {x=0, y=0, w=100, h=100})
    if type(tolerance) == "table" then
        rect_table = tolerance
        tolerance = nil
    end

    if type(reverse) ~= "boolean" then
        reverse = false
    end

    if type(reverse_vertical) ~= "boolean" then
        reverse_vertical = false
    end

    local rect = make_rect(
        rect_table and rect_table.x, 
        rect_table and rect_table.y, 
        rect_table and rect_table.w, 
        rect_table and rect_table.h,
        width,
        height
    )

    local res
    -- If tolerance is nil, 0, or not provided, use the faster exact match
    if not tolerance or tolerance == 0 then
        res = ffi.C.find_exact_color_ffi(
            buffer, width, height, stride, rect, r, g, b, reverse, reverse_vertical
        )
    else
        res = ffi.C.find_fuzzy_color_ffi(
            buffer, width, height, stride, rect, r, g, b, tolerance, reverse, reverse_vertical
        )
    end
    
    if res.found then
        return { x = res.x, y = res.y }
    end
    return nil
end

function system.screen.find_color(r, g, b, tolerance, rect_table, reverse, reverse_vertical) 
    local h = state.shm_header
    if h == ffi.NULL then return nil end
    return _find_color(r, g, b, tolerance, rect_table, reverse, reverse_vertical, state.pixel_data, h.width, h.height, h.stride)
end

local PixelBuffer_t = ffi.typeof("PixelBuffer")
local PixelBufferPtr_t = ffi.typeof("PixelBuffer*")

function _lua_OpenCVMat_free(obj)
    if obj.mat_ptr == ffi.NULL then return end
    ffi.C._OpenCVMat_free(obj)
    obj.mat_ptr = ffi.NULL
end

local common_methods_pixelbuffer = {
    crop = function(self, region)
        local x, y, w, h = validate_region(region, self.width, self.height)
        
        -- Use the pointer dereference trick to pass by value to C++
        local casted_src = ffi.cast(PixelBufferPtr_t, self)[0]
        local res = ffi.C.crop_buffer(casted_src, x, y, w, h)
        
        if res.data == nil then return nil end

        return res
    end,
    
    copy = function(self)
        local casted_src = ffi.cast(PixelBufferPtr_t, self)[0]
        local res = ffi.C.copy_buffer(casted_src)
        
        if res.data == nil then return nil end

        return res
    end,
    
    find_color = function(self, r, g, b, tolerance, rect_table, reverse, reverse_vertical)
        local buffer = self.data
        return _find_color(r, g, b, tolerance, rect_table, reverse, reverse_vertical, buffer, self.width, self.height, self.stride)
    end,

    to_opencv_mat = function(self, options)
        local readonly = options ~= nil and options.copy == false
        local casted_self = ffi.cast(PixelBufferPtr_t, self)[0]
        
        collectgarbage("step", tonumber(self.width*self.height*12/1024))
        return ffi.gc(ffi.C._PixelBuffer_to_OpenCVMat(readonly, casted_self), _lua_OpenCVMat_free)
    end,

    save_frame = function(self, filename)
        return system.save_frame(self, filename)
    end
}

local structColorPtr_t = ffi.typeof("struct Color*")

function get_pixel(self, x, y)
    -- 1. Bounds check (Crucial to prevent out-of-bounds memory access)
    if x < 0 or x >= self.width or y < 0 or y >= self.height then 
        return nil 
    end
    
    -- 2. Calculate the exact byte offset
    local idx = (y * self.stride) + (x * 4)
    
    -- 3. Cast the pointer at that specific address to a Color struct pointer
    -- We use self.data + idx to get the memory address of that specific pixel
    return ffi.cast(structColorPtr_t, (self.data + idx))
end

local structColor_t = ffi.typeof("struct Color")

function read_pixel(self, x, y)
    -- 1. Bounds check (Crucial to prevent out-of-bounds memory access)
    if x < 0 or x >= self.width or y < 0 or y >= self.height then 
        return nil 
    end
    
    -- 2. Calculate the exact byte offset
    local idx = (y * self.stride) + (x * 4)
    
    -- 3. Cast the pointer at that specific address to a Color struct pointer
    -- We use self.data + idx to get the memory address of that specific pixel
    return ffi.cast(structColor_t, (self.data + idx))
end

function free_pixelbuffer(self)
    local casted_self = ffi.cast(PixelBufferPtr_t, self)[0]
    ffi.C.free_buffer(casted_self)
    self.data = ffi.NULL
    self = nil
    casted_self = nil
end
ffi.metatype("PixelBufferReadOnly", {
    __index = setmetatable({
        get_pixel = function(...) error("get_pixel cannot be used on read only pixel buffers! Use read_pixel instead.", 2) end,
        read_pixel = read_pixel
    }, { __index = common_methods_pixelbuffer })
})

-- Apply methods to the Mutable type
ffi.metatype("PixelBuffer", {
    __index = setmetatable({
        get_pixel = get_pixel,
        read_pixel = read_pixel,
        free = free_pixelbuffer
    }, { __index = common_methods_pixelbuffer }),
    __gc = function(obj)
        local casted = ffi.cast(PixelBufferPtr_t, obj)[0]
        ffi.C.free_buffer(obj)
    end
})

system.opencv = system.opencv or {}

local OpenCVMat_empty = ffi.new("OpenCVMat")
OpenCVMat_empty.channels = 0

function system.opencv.load_from_file(filename)
    local mat = ffi.gc(ffi.C._OpenCVMat_load_from_file(filename), _lua_OpenCVMat_free)
    collectgarbage("step", tonumber(mat.width*mat.height*mat.channels/1024))
    return mat
end

local opencvmat_methods = {
    free = function(self)
        _lua_OpenCVMat_free(self)
        self = nil
    end,
    to_bgr = function(self)
        if self.channels ~= 4 then error("Mat is not in BGRA!", 2) end
        collectgarbage("step", tonumber(self.width*self.height*3/1024))
        return ffi.gc(ffi.C._OpenCVMat_bgra_to_bgr(self), _lua_OpenCVMat_free)
    end,
    to_bgra = function(self)
        if self.channels ~= 4 then error("Mat is not in BGR!", 2) end

        collectgarbage("step", tonumber(self.width*self.height*4/1024))
        return ffi.gc(ffi.C._OpenCVMat_bgr_to_bgra(self), _lua_OpenCVMat_free)
    end,
    to_pixelbuffer = function(self)
        collectgarbage("step", tonumber(self.width*self.height*4/1024))
        return ffi.gc(ffi.C._OpenCVMat_to_PixelBuffer(self), free_pixelbuffer)
    end,
    resize = function(self, w, h)
        collectgarbage("step", tonumber(w*h*self.channels/1024))
        return ffi.gc(ffi.C._OpenCVMat_resize(self, w, h), _lua_OpenCVMat_free)
    end,
    upsample = function(self)
        collectgarbage("step", tonumber(self.width*self.height*self.channels/256))
        return ffi.gc(ffi.C._OpenCVMat_upsample(self), _lua_OpenCVMat_free)
    end,
    downsample = function(self)
        collectgarbage("step", tonumber(self.width*self.height/1024))
        return ffi.gc(ffi.C._OpenCVMat_downsample(self), _lua_OpenCVMat_free)
    end,
    match_template = function(self, template, options)
        local threshold = -1
        local raw = false
        local mask,scene_pyr,template_pyr,mask_pyr = nil, nil, nil, nil
        if options ~= nil then
            if options.threshold ~= nil and (type(options.threshold) ~= "number" or options.threshold < 0 or options.threshold > 1) then error("options.threshold must be a float from 0 to 1!") end
            threshold = options.threshold or -1
            raw = options.raw
            if raw ~= nil and raw ~= true and raw ~= false then error("options.raw must be a boolean!") end
            raw = options.raw ~= nil and options.raw or false

            mask = options.mask or OpenCVMat_empty
            scene_pyr = options.scene_downsampled or OpenCVMat_empty
            template_pyr = options.template_downsampled or OpenCVMat_empty
            mask_pyr = options.mask_downsampled or OpenCVMat_empty
        end

        if raw then return ffi.gc(ffi.C._OpenCVMat_match_template(self, template, threshold, mask), ffi.C._OpenCVTemplateMatchVector_free) end
        return ffi.gc(ffi.C._OpenCVMat_match_template_pyr(self, template, threshold, mask, scene_pyr, template_pyr, mask_pyr), ffi.C._OpenCVTemplateMatchVector_free)
    end,
    extract_channel = function(self, channel)
        collectgarbage("step", tonumber(self.width*self.height/1024))
        return ffi.gc(ffi.C._OpenCVMat_extract_channel(self, channel-1), _lua_OpenCVMat_free)
    end
}

ffi.metatype("OpenCVMat", {
    __index = opencvmat_methods,
    free = function(self)
        _lua_OpenCVMat_free(self)
    end,
    __gc = function(self)
        _lua_OpenCVMat_free(self)
    end
})
-- DEBUG: Verify Lua sees the same address as C++

system.screen = system.screen or {}

local screen_mt = {
    __index = function(t, k)
        if state == nil or state.shm_header == ffi.NULL then 
            return nil 
        end

        -- Ensure we don't return 0 for dimensions if we're "ready"
        local w = state.shm_header.width
        local h = state.shm_header.height
        
        if k == "ready"  then return (w > 0 and h > 0) end
        if k == "width"  then return tonumber(w) end
        if k == "height" then return tonumber(h) end
        if k == "stride" then return tonumber(state.shm_header.stride) end
        if k == "index" then return tonumber(state.shm_header.frame_index) end
        if k == "timestamp" then return tonumber(state.shm_header.capture_timestamp) end
        if k == "buffer" then return state.buffer end
        
        return rawget(t, k)
    end
}

setmetatable(system.screen, screen_mt)

function system.screen.wait_until_ready()
    co = coroutine.running()
    if not co then
        while not system.screen.ready do
            system._poll_events()
            system.wait(10, "ms")
        end
    else
        while not system.screen.ready do
            coroutine.yield()
            system.wait(10, "ms")
        end
    end
end

local voidptr_t = ffi.typeof("void*")
local uintptr_t = ffi.typeof("uintptr_t")

local function normalize_path(path)
    local is_absolute = string.sub(path, 1, 1) == "/"
    local parts = {}
    for part in string.gmatch(path, "[^/]+") do
        if part == ".." then
            if #parts > 0 then
                table.remove(parts)
            end
        elseif part ~= "." and part ~= "" then
            table.insert(parts, part)
        end
    end
    local normalized = table.concat(parts, "/")
    if is_absolute then
        return "/" .. normalized
    end
    return normalized
end

local function resolve_output_path(path)
    if type(path) ~= "string" or path == "" then
        error("save_frame requires a non-empty string path", 3)
    end

    if string.sub(path, 1, 1) == "/" then
        return normalize_path(path)
    end

    if string.sub(path, 1, 2) == "~/" then
        local home = os and os.getenv and os.getenv("HOME")
        if home and home ~= "" then
            return normalize_path(home .. "/" .. string.sub(path, 3))
        end
    end

    local base = project_root
    if type(base) ~= "string" or base == "" then
        return normalize_path(path)
    end

    return normalize_path(base .. "/" .. path)
end

function system.save_frame(buffer, filename)
    if buffer == nil or buffer.data == nil then
        error("save_frame requires a PixelBuffer-like object as first argument", 2)
    end
    local resolved = resolve_output_path(filename)
    return ffi.C._save_frame_image(buffer.data, buffer.width, buffer.height, buffer.stride, resolved)
end

function system.screen.save_frame(filename)
    local h = state.shm_header
    if h == ffi.NULL then return false end
    return system.save_frame(state.buffer, filename)
end

system.fs = system.fs or {}
local fs_native = system.fs
local fs_open_native = fs_native.open
local fs_read_all_native = fs_native.read_all
local fs_write_all_native = fs_native.write_all
local fs_exists_native = fs_native.exists
local fs_stat_native = fs_native.stat
local fs_list_native = fs_native.list
local fs_remove_native = fs_native.remove
local fs_mkdir_native = fs_native.mkdir
local fs_rename_native = fs_native.rename
local fs_read_native = fs_native._read
local fs_write_native = fs_native._write
local fs_seek_native = fs_native._seek
local fs_flush_native = fs_native._flush
local fs_close_native = fs_native._close

local fs_file_handle_mt = {}
fs_file_handle_mt.__index = fs_file_handle_mt

function fs_file_handle_mt:read(spec)
    return fs_read_native(self._id, spec)
end

function fs_file_handle_mt:write(data)
    return fs_write_native(self._id, data == nil and "" or tostring(data))
end

function fs_file_handle_mt:seek(whence, offset)
    return fs_seek_native(self._id, whence, offset)
end

function fs_file_handle_mt:flush()
    return fs_flush_native(self._id)
end

function fs_file_handle_mt:close()
    if self._id == nil then return true end
    fs_close_native(self._id)
    self._id = nil
    return true
end

function fs_file_handle_mt:lines()
    return function()
        local line = self:read("*l")
        if line == nil then return nil end
        return line
    end
end

function system.fs.open(path, mode)
    local id = fs_open_native(path, mode)
    return setmetatable({_id = id}, fs_file_handle_mt)
end

function system.fs.read_all(path)
    return fs_read_all_native(path)
end

function system.fs.write_all(path, data)
    return fs_write_all_native(path, data == nil and "" or tostring(data), false)
end

function system.fs.append(path, data)
    return fs_write_all_native(path, data == nil and "" or tostring(data), true)
end

function system.fs.exists(path)
    return fs_exists_native(path)
end

function system.fs.stat(path)
    return fs_stat_native(path)
end

function system.fs.list(path)
    return fs_list_native(path)
end

function system.fs.remove(path)
    return fs_remove_native(path)
end

function system.fs.mkdir(path, recursive)
    return fs_mkdir_native(path, recursive)
end

function system.fs.rename(src, dst)
    return fs_rename_native(src, dst)
end

-- 5. Task and Timer Management (Existing Logic)
system._tasks = {}
system._active_timers = {}
system._next_timer_id = 1

local function to_seconds(time, unit)
    if unit == "ms" then return time / 1000 end
    if unit == "us" then return time / 1000000 end
    return time
end

-- Replace system._update_tasks with this:
function system._update_tasks()
    local now = system.get_time()
    for i = #system._tasks, 1, -1 do
        local task = system._tasks[i]
        
        -- Check if task is ready OR if we had a massive time skip (OS Wakeup)
        if not task.wake_at or now >= task.wake_at then
            local ok, msg, param = coroutine.resume(task.co)

            if system._apply_cpu_throttle then
                system._apply_cpu_throttle()
            end

            if not ok then
                -- Log the error so you can see WHY it stopped
                print("[TASK CRASH]: " .. tostring(msg))
                table.remove(system._tasks, i)
            elseif coroutine.status(task.co) == "dead" then
                table.remove(system._tasks, i)
            elseif msg == "sleep" then
                task.wake_at = param
            else
                task.wake_at = nil
            end
        end
    end
end

-- Update task creation to handle the wrapper table
function system._create_task(f)
    table.insert(system._tasks, { co = coroutine.create(f), wake_at = nil })
end

local function clear_timer(id)
    if id then system._active_timers[id] = nil end
end

system.clear_timeout = clear_timer
system.clear_interval = clear_timer

local native_wait = system._cpp_wait
function system.wait(s, unit)
    local duration = to_seconds(s, unit)
    local co = coroutine.running()
    
    if co then
        -- Yield a specific signal and a target wake-up time
        coroutine.yield("sleep", system.get_time() + duration)
    else
        native_wait(s, unit)
    end
end

function system.set_timeout(f, s, unit)
    local id = system._next_timer_id
    system._next_timer_id = id + 1
    system._active_timers[id] = true
    system._create_task(function()
        system.wait(s, unit)
        if system._active_timers[id] then
            local success, err = pcall(f)
            if not success then print("[TIMEOUT ERROR]: " .. tostring(err)) end
            clear_timer(id)
        end
    end)
    return id
end

function system.set_interval(f, s, unit)
    local id = system._next_timer_id
    system._next_timer_id = id + 1
    system._active_timers[id] = true
    local duration = to_seconds(s, unit)
    system._create_task(function()
        local target = system.get_time() + duration
        while system._active_timers[id] do
            local success, err = pcall(f)
            if not success then 
                print("[INTERVAL ERROR]: " .. tostring(err))
                break 
            end
            local delay = target - system.get_time()
            if delay > 0 then system.wait(delay) end
            target = target + duration
        end
    end)
    return id
end

function system.screen.get_next_frame(timeout_ms)
    -- 1. Check if we are actually in a coroutine/task
    local co = coroutine.running()
    if not co then
        -- If called from the main global scope, we can't yield.
        -- We'll just have to do a hard poll (less ideal)
        local start = system.screen.index
        while system.screen.index == start do 
            system._poll_events()
            system.wait(10, "us")
        end
        return true
    end

    -- 2. Coroutine-friendly waiting
    local start_index = system.screen.index
    local start_time = system.get_time("ms")
    local timeout = timeout_ms or 1000 -- Default 1s timeout

    while system.screen.index == start_index do
        -- Check for timeout to prevent infinite hangs
        if system.get_time("ms") - start_time > timeout then
            return false, "timeout"
        end

        -- Yield control back to C++ (poll_events)
        coroutine.yield()
    end

    return true
end


local function vector_iter(self, i)
    i = i + 1
    if i <= tonumber(self.size) then
        -- This leverages your existing __index logic 
        -- (including the ptr[0] dereference and cast)
        return i, self[i]
    end
end

local LuaOCRResultPtr_t = ffi.typeof("LuaOCRResult*")
local OCRVector_methods = {
    find = function(self, target)
        for i, res in ipairs(self) do
            if res.text == target then
                return res -- Returns the OCRResult struct
            end
        end
        return nil
    end
}
local OCRVector_mt = {
    __index = function(self, key)
        if type(key) == "number" then
            -- Bounds Check: 1-indexed for Lua
            if key >= 1 and key <= tonumber(self.size) then
                return ffi.cast(LuaOCRResultPtr_t, self.data[key - 1])
            elseif key == "free" then
                return ffi.C._free_OCRVector
            else
                -- Match Lua table behavior: return nil if past the end
                return nil
            end
        elseif key == "find" then return OCRVector_methods.find
        end
        return nil
    end,
    
    __len = function(self)
        return tonumber(self.size)
    end,

    __ipairs = function(self)
        return vector_iter, self, 0
    end,

    __gc = function(obj)
        ffi.C._free_OCRVector(obj)
    end
}

local OpenCVTemplateMatchPtr_t = ffi.typeof("OpenCVTemplateMatch*")
local OpenCVTemplateMatchVector_mt = {
    __index = function(self, key)
        if type(key) == "number" then
            -- Bounds Check: 1-indexed for Lua
            if key >= 1 and key <= tonumber(self.size) then
                return ffi.cast(OpenCVTemplateMatchPtr_t, self.data[key - 1])
            elseif key == "free" then
                return ffi.C._OpenCVTemplateMatchVector_free
            else
                -- Match Lua table behavior: return nil if past the end
                return nil
            end
        end
        return nil
    end,
    
    __len = function(self)
        return tonumber(self.size)
    end,

    __ipairs = function(self)
        return vector_iter, self, 0
    end,

    __gc = function(obj)
        ffi.C._OpenCVTemplateMatchVector_free(obj)
    end
}
local LuaOCRResult_mt = {
    __index = function(self, key)
        if key == "text" then
            -- When the user types 'result.text', they get a Lua string
            -- instead of the raw cdata char array
            return ffi.string(self._text)
        end
        -- Default behavior: look up other fields (x, y, w, h)
        return nil 
    end
}

ffi.metatype("LuaOCRResult", LuaOCRResult_mt)

ffi.metatype("OCRVector", OCRVector_mt)

ffi.metatype("OpenCVTemplateMatchVector", OpenCVTemplateMatchVector_mt)

system.screen.ocr = {fast = {}, accurate = {}}

function system.screen.ocr.fast.recognize_text(buffer, options)
    if buffer == system.screen.ocr.fast then
        buffer = nil
    end
    if type(buffer) == "table" then options = buffer; buffer = nil end

    local using_region = options ~= nil and options.region ~= nil
    local x, y, w, h = 0, 0, 0, 0
    if using_region then
        x, y, w, h = options.region.x, options.region.y, options.region.w, options.region.h
    end

    if buffer ~= nil then return ffi.gc(ffi.C._recognize_text(true, buffer.data, buffer.width, buffer.height, buffer.stride, using_region, x, y, w, h), ffi.C._free_OCRVector) end

    local header = state.shm_header
    return ffi.gc(ffi.C._recognize_text(true, state.pixel_data, header.width, header.height, header.stride, using_region, x, y, w, h), ffi.C._free_OCRVector)
end

function system.screen.ocr.accurate.recognize_text(buffer, options)
    if buffer == system.screen.ocr.accurate then
        buffer = nil
    end
    if type(buffer) == "table" then options = buffer; buffer = nil end

    local using_region = options ~= nil and options.region ~= nil
    local x, y, w, h = 0, 0, 0, 0
    if using_region then
        x, y, w, h = options.region.x, options.region.y, options.region.w, options.region.h
    end
    
    if buffer ~= nil then
        local casted = ffi.cast(PixelBufferPtr_t, buffer)[0]
        return ffi.gc(ffi.C._recognize_text(false, casted.data, buffer.width, buffer.height, buffer.stride, using_region, x, y, w, h), ffi.C._free_OCRVector)
    end

    local header = state.shm_header
    return ffi.gc(ffi.C._recognize_text(false, state.pixel_data, header.width, header.height, header.stride, using_region, x, y, w, h), ffi.C._free_OCRVector)
end

local function wrap_object_methods(object_table, method_names)
    if type(object_table) ~= "table" then return end
    for _, method_name in ipairs(method_names) do
        local original = object_table[method_name]
        if type(original) == "function" then
            object_table[method_name] = function(...)
                if select(1, ...) == object_table then
                    return original(select(2, ...))
                end
                return original(...)
            end
        end
    end
end

wrap_object_methods(system.screen, {
    "wait_until_ready",
    "get_next_frame",
    "save_frame",
    "save_screenshot",
    "find_color",
})
wrap_object_methods(system.mouse, {
    "move",
    "click",
    "double_click",
    "down",
    "up",
    "drag",
    "scroll",
    "get_position",
    "send",
})
wrap_object_methods(system.keyboard, {
    "press",
    "down",
    "up",
    "type",
    "send",
})
wrap_object_methods(system.opencv, {
    "load_from_file",
})
wrap_object_methods(system.screen.ocr.fast, {"recognize_text"})
wrap_object_methods(system.screen.ocr.accurate, {"recognize_text"})

-- 1. Define the whitelist of modules the user is allowed to 'require'
local allowed_modules = {
    ["math"] = true,
    ["string"] = true,
    ["table"] = true,
    ["bit"] = true,
}
local user_env

local manifest_lua_files = system._manifest_lua_files or {}
--system._project_dir = nil
system._manifest_lua_files = nil

local function starts_with(value, prefix)
    return string.sub(value, 1, #prefix) == prefix
end

local function resolve_module_to_path(module_name)
    local mapped = module_name:gsub("%.", "/")
    if not mapped:match("%.lua$") then
        mapped = mapped .. ".lua"
    end

    local candidate
    if string.sub(mapped, 1, 1) == "/" then
        candidate = normalize_path(mapped)
    else
        candidate = normalize_path(project_root .. "/" .. mapped)
    end

    local normalized_root = normalize_path(project_root)
    if candidate ~= normalized_root and not starts_with(candidate, normalized_root .. "/") then
        error("Access denied: module path escapes project root: '" .. module_name .. "'", 2)
    end
    return candidate
end

local project_module_cache = {}

local function load_project_module(module_name)
    local resolved_path = resolve_module_to_path(module_name)
    if not manifest_lua_files[resolved_path] then
        error("Access denied: module '" .. module_name .. "' is not listed in manifest.files", 2)
    end

    if project_module_cache[resolved_path] ~= nil then
        return project_module_cache[resolved_path]
    end
    if package.loaded[module_name] ~= nil then
        return package.loaded[module_name]
    end

    local chunk, err = loadfile(resolved_path)
    if not chunk then
        error("Failed to load module '" .. module_name .. "': " .. tostring(err), 2)
    end
    setfenv(chunk, user_env)

    local result = chunk()
    if result == nil then
        result = true
    end
    project_module_cache[resolved_path] = result
    package.loaded[module_name] = result
    return result
end

-- 2. Create the custom restricted require
local function safe_require(module_name)
    if allowed_modules[module_name] then
        return require(module_name)
    end
    return load_project_module(module_name)
end

-- 3. Define the User Environment (The Sandbox)
user_env = {
    -- Include basic safe Lua globals
    print = print,
    tostring = tostring,
    tonumber = tonumber,
    pairs = pairs,
    ipairs = ipairs,
    next = next,
    pcall = pcall,
    xpcall = xpcall,
    error = error,
    assert = assert,
    type = type,
    select = select,
    error = error,
    collectgarbage = collectgarbage,

    -- Include your custom API
    system = system, 
    
    -- Include the restricted require
    require = safe_require,
    
    -- Provide safe versions of libraries by default
    math = math,
    string = string,
    table = table,
    bit = bit,
    --fs = system.fs,
    --ffi = ffi
}

-- Point the sandbox's _G to itself so user can't find the real _G
user_env._G = user_env

function _run_code_str(code_string, chunk_name)
    local chunk, err = load(code_string, chunk_name or "user_script", "t")
    if not chunk then return false, "Syntax Error: " .. err end
    
    setfenv(chunk, user_env)

    -- xpcall allows us to catch the error AND the stack trace
    local success, result = xpcall(chunk, debug.traceback)
    
    if not success then
        print("[LUA ERROR]: " .. result) -- This will now show up in your console
    end
    
    return success, result
end