// Custom Lua API Completions for Macro Runner
// Import this in your main.js and call registerLuaAPI(monaco) after editor is created
let monaco = null;

// Type definitions for return values (for basic type inference)
const typeDefinitions = {
    // Table types that have properties/methods
    'table': {
        x: { type: 'number', description: 'X coordinate' },
        y: { type: 'number', description: 'Y coordinate' },
        w: { type: 'number', description: 'Width' },
        h: { type: 'number', description: 'Height' }
    }
};

export const luaAPI = {
    system: {
        // Event handling
        on_key: {
            description: "Registers a keyboard shortcut handler",
            overloads: [
                {
                    params: [
                        { name: "combo", type: "string" },
                        { name: "callback", type: "function" }
                    ],
                    returns: "void",
                    documentation: "Registers a keybind\n\nExample: system.on_key('cmd+space', function() print('triggered') end)"
                },
                {
                    params: [
                        { name: "combo", type: "string" },
                        { name: "options", type: "table" },
                        { name: "callback", type: "function" }
                    ],
                    returns: "void",
                    documentation: "Registers a keybind with options\n\nExample: system.on_key('cmd+space', {swallow = true}, callback)"
                }
            ]
        },
        remove_keybind: {
            description: "Removes a previously registered keybind",
            overloads: [
                { params: [{ name: "combo", type: "string" }], returns: "void", documentation: "Removes the keybind for the specified combo" }
            ]
        },
        
        // Timing functions
        wait: {
            description: "Suspends execution for a duration",
            overloads: [
                { params: [{ name: "time", type: "number" }], returns: "void", documentation: "Waits for time in seconds (default)" },
                { params: [{ name: "time", type: "number" }, { name: "unit", type: "'s'|'ms'|'us'" }], returns: "void", documentation: "Waits for time in specified unit (s/ms/us)" }
            ]
        },
        get_time: {
            description: "Returns high-resolution steady clock time",
            overloads: [
                { params: [], returns: "number", documentation: "Returns time in seconds" },
                { params: [{ name: "unit", type: "'s'|'ms'|'us'" }], returns: "number", documentation: "Returns time in specified unit" }
            ]
        },
        set_timeout: {
            description: "Schedules a one-time task",
            overloads: [
                {
                    params: [{ name: "callback", type: "function" }, { name: "time", type: "number" }],
                    returns: "integer",
                    documentation: "Runs callback once after time (seconds)"
                },
                {
                    params: [{ name: "callback", type: "function" }, { name: "time", type: "number" }, { name: "unit", type: "'s'|'ms'|'us'" }],
                    returns: "integer",
                    documentation: "Runs callback once after time in specified unit"
                }
            ]
        },
        set_interval: {
            description: "Schedules a repeating task",
            overloads: [
                {
                    params: [{ name: "callback", type: "function" }, { name: "time", type: "number" }],
                    returns: "integer",
                    documentation: "Runs callback repeatedly every time (seconds)"
                },
                {
                    params: [{ name: "callback", type: "function" }, { name: "time", type: "number" }, { name: "unit", type: "'s'|'ms'|'us'" }],
                    returns: "integer",
                    documentation: "Runs callback repeatedly in specified unit"
                }
            ]
        },
        clear_timeout: {
            description: "Cancels a scheduled timeout",
            overloads: [
                { params: [{ name: "timer_id", type: "integer" }], returns: "void", documentation: "Cancels the timeout with the given ID" }
            ]
        },
        clear_interval: {
            description: "Cancels a scheduled interval",
            overloads: [
                { params: [{ name: "timer_id", type: "integer" }], returns: "void", documentation: "Cancels the interval with the given ID" }
            ]
        },
        save_frame: {
            description: "Saves a PixelBuffer to an image file",
            overloads: [
                {
                    params: [{ name: "buffer", type: "PixelBuffer|PixelBufferReadOnly" }, { name: "path", type: "string" }],
                    returns: "boolean",
                    documentation: "Saves frame buffer using extension format (png/jpg/jpeg/bmp/tiff/webp/ppm). Relative paths resolve from macro project directory; absolute paths are used as-is."
                }
            ]
        },
        exit: {
            description: "Terminates the runner process",
            overloads: [
                { params: [], returns: "void", documentation: "Exits with code 0" },
                { params: [{ name: "exit_code", type: "integer" }], returns: "void", documentation: "Exits with specified code" }
            ]
        },
        _create_task: {
            description: "Creates a background task that runs concurrently",
            overloads: [
                { params: [{ name: "callback", type: "function" }], returns: "void", documentation: "Runs callback as a background task\n\nExample: system._create_task(function() while true do print('tick') system.wait(1) end end)" }
            ]
        }
    },
    
    stats: {
        get_info: {
            description: "Returns system resource usage statistics",
            overloads: [
                { params: [], returns: "table", documentation: "Returns {cpu: number, ram: number} in percentage and bytes" },
                { params: [{ name: "unit", type: "'mb'|'gb'" }], returns: "table", documentation: "Returns {cpu: number, ram: number} with RAM in specified unit" }
            ]
        }
    },

    ui: {
        open: {
            description: "Opens a macro UI window and returns a handle",
            overloads: [
                {
                    params: [{ name: "path", type: "string" }],
                    returns: "UIWindowHandle",
                    documentation: "Opens a new UI window from a manifest-listed HTML path"
                },
                {
                    params: [{ name: "path", type: "string" }, { name: "options", type: "table" }],
                    returns: "UIWindowHandle",
                    documentation: "Opens a UI window with options\n\nOptions: {title?: string, x?: number, y?: number, width?: number, height?: number, w?: number, h?: number}"
                }
            ]
        },
        close: {
            description: "Closes a UI window",
            overloads: [
                { params: [], returns: "void", documentation: "Closes the most recently opened UI window" },
                { params: [{ name: "window_id", type: "integer" }], returns: "void", documentation: "Closes a specific UI window by id" }
            ]
        },
        run_js: {
            description: "Runs JavaScript in a UI window",
            overloads: [
                { params: [{ name: "js", type: "string" }], returns: "void", documentation: "Runs JS in the most recently opened UI window" },
                { params: [{ name: "js", type: "string" }, { name: "window_id", type: "integer" }], returns: "void", documentation: "Runs JS in a specific UI window by id" }
            ]
        },
        on: {
            description: "Registers a UI event callback",
            overloads: [
                { params: [{ name: "event", type: "string" }, { name: "callback", type: "function" }], returns: "void", documentation: "Callback receives payload and window_id: function(payload, window_id)" }
            ]
        },
        off: {
            description: "Unregisters a UI event callback",
            overloads: [
                { params: [{ name: "event", type: "string" }], returns: "void", documentation: "Removes callback for event name" }
            ]
        }
    },
    
    screen: {
        // Properties documented in hover
        _properties: ["ready", "width", "height", "stride", "index", "timestamp"],
        
        wait_until_ready: {
            description: "Blocks until screen buffer is initialized",
            overloads: [
                { params: [], returns: "void", documentation: "Waits until first frame is captured" }
            ]
        },
        get_dimensions: {
            description: "Returns screen dimensions from main process",
            overloads: [
                { params: [], returns: "number, number", documentation: "Returns width, height" }
            ]
        },
        get_current_timestamp: {
            description: "Returns the timestamp of the current frame",
            overloads: [
                { params: [], returns: "number", documentation: "Returns timestamp in microseconds" }
            ]
        },
        begin_capture: {
            description: "Starts screen capture",
            overloads: [
                { params: [], returns: "void", documentation: "Captures at 60fps, full screen" },
                { params: [{ name: "fps", type: "integer" }], returns: "void", documentation: "Captures at specified FPS" },
                { params: [{ name: "fps", type: "integer" }, { name: "region", type: "table" }], returns: "void", documentation: "Captures region at FPS\n\nExample: begin_capture(30, {x=0, y=0, w=1920, h=1080})" },
                { params: [{ name: "callback", type: "function" }, { name: "fps", type: "integer" }], returns: "void", documentation: "Captures with frame callback" },
                { params: [{ name: "callback", type: "function" }, { name: "region", type: "table" }], returns: "void", documentation: "Captures region with callback" },
                { params: [{ name: "callback", type: "function" }, { name: "fps", type: "integer" }, { name: "region", type: "table" }], returns: "void", documentation: "Full capture with callback, FPS, and region" }
            ]
        },
        stop_capture: {
            description: "Stops active screen capture",
            overloads: [
                { params: [], returns: "void", documentation: "Stops the current capture session" }
            ]
        },
        stop_capture: {
            description: "Stops active screen capture",
            overloads: [
                { params: [], returns: "void", documentation: "Stops the current capture session" }
            ]
        },
        get_next_frame: {
            description: "Yields until a new frame arrives",
            overloads: [
                { params: [], returns: "boolean, string?", documentation: "Waits for next frame (1s timeout)" },
                { params: [{ name: "timeout_ms", type: "number" }], returns: "boolean, string?", documentation: "Waits with custom timeout\n\nReturns: success, error_message" }
            ]
        },
        get_pixel: {
            description: "Returns color of a specific pixel",
            overloads: [
                {
                    params: [{ name: "x", type: "number" }, { name: "y", type: "number" }],
                    returns: "table",
                    documentation: "Returns color table {r, g, b, a} (0-255)"
                }
            ]
        },
        find_color: {
            description: "Searches for a color within tolerance",
            overloads: [
                {
                    params: [{ name: "r", type: "number" }, { name: "g", type: "number" }, { name: "b", type: "number" }, { name: "tolerance", type: "number" }],
                    returns: "table?",
                    documentation: "Returns {x, y} if found (full screen search)"
                },
                {
                    params: [
                        { name: "r", type: "number" },
                        { name: "g", type: "number" },
                        { name: "b", type: "number" },
                        { name: "tolerance", type: "number" },
                        { name: "region", type: "table" }
                    ],
                    returns: "table?",
                    documentation: "Returns {x, y} if found in region\n\nExample: find_color(255, 0, 0, 10, {x=100, y=100, w=200, h=200})"
                },
                {
                    params: [
                        { name: "r", type: "number" },
                        { name: "g", type: "number" },
                        { name: "b", type: "number" },
                        { name: "tolerance", type: "number" },
                        { name: "region", type: "table" },
                        { name: "reverse", type: "boolean" }
                    ],
                    returns: "table?",
                    documentation: "Returns {x, y} if found in region\n\nReverse: search from right to left when true"
                }
            ]
        },
        find_all_colors: {
            description: "Finds all occurrences of a color",
            overloads: [
                {
                    params: [{ name: "r", type: "number" }, { name: "g", type: "number" }, { name: "b", type: "number" }, { name: "tolerance", type: "number" }],
                    returns: "table",
                    documentation: "Returns array of {x, y} tables (full screen)"
                },
                {
                    params: [
                        { name: "r", type: "number" },
                        { name: "g", type: "number" },
                        { name: "b", type: "number" },
                        { name: "tolerance", type: "number" },
                        { name: "region", type: "table" }
                    ],
                    returns: "table",
                    documentation: "Returns array of {x, y} tables in region"
                }
            ]
        },
        find_image: {
            description: "Template matching for image detection",
            overloads: [
                {
                    params: [{ name: "template_path", type: "string" }],
                    returns: "number?, number?",
                    documentation: "Returns x, y of top-left corner if found (full screen, 90% threshold)"
                },
                {
                    params: [{ name: "template_path", type: "string" }, { name: "threshold", type: "number" }],
                    returns: "number?, number?",
                    documentation: "Returns x, y if found (full screen, custom threshold 0-1)"
                },
                {
                    params: [{ name: "template_path", type: "string" }, { name: "threshold", type: "number" }, { name: "region", type: "table" }],
                    returns: "number?, number?",
                    documentation: "Returns x, y if found in region"
                }
            ]
        },
        find_all_images: {
            description: "Finds all matches of a template",
            overloads: [
                {
                    params: [{ name: "template_path", type: "string" }],
                    returns: "table",
                    documentation: "Returns array of {x, y, score} tables"
                },
                {
                    params: [{ name: "template_path", type: "string" }, { name: "threshold", type: "number" }],
                    returns: "table",
                    documentation: "Returns all matches above threshold"
                },
                {
                    params: [{ name: "template_path", type: "string" }, { name: "threshold", type: "number" }, { name: "region", type: "table" }],
                    returns: "table",
                    documentation: "Returns all matches in region"
                }
            ]
        },
        save_screenshot: {
            description: "Saves current frame to file",
            overloads: [
                {
                    params: [{ name: "path", type: "string" }],
                    returns: "boolean",
                    documentation: "Alias for save_frame(path). Saves using extension format (png/jpg/jpeg/bmp/tiff/webp/ppm). Relative paths resolve from macro project directory."
                }
            ]
        },
        save_frame: {
            description: "Saves current frame to file",
            overloads: [
                {
                    params: [{ name: "path", type: "string" }],
                    returns: "boolean",
                    documentation: "Saves current frame using extension format (png/jpg/jpeg/bmp/tiff/webp/ppm). Relative paths resolve from macro project directory."
                }
            ]
        },
        
        canvas: {
            text: {
                description: "Renders text on the overlay",
                overloads: [
                    { params: [{ name: "text", type: "string" }, { name: "x", type: "number" }, { name: "y", type: "number" }, { name: "color", type: "number" }], returns: "void", documentation: "Draws text at position with RGBA color (0xRRGGBBAA)" },
                    { params: [{ name: "text", type: "string" }, { name: "x", type: "number" }, { name: "y", type: "number" }, { name: "color", type: "number" }, { name: "options", type: "table" }], returns: "void", documentation: "Draws text with options\n\nOptions: {id: string, thickness: float, classes: table}" }
                ]
            },
            rect: {
                description: "Draws a rectangle on the overlay",
                overloads: [
                    { params: [{ name: "rect", type: "table" }, { name: "color", type: "number" }], returns: "void", documentation: "Draws rectangle outline with RGBA color\n\nRect: {x, y, w, h}\nColor: 0xRRGGBBAA" },
                    { params: [{ name: "rect", type: "table" }, { name: "color", type: "number" }, { name: "options", type: "table" }], returns: "void", documentation: "Draws rectangle with options\n\nRect: {x, y, w, h}\nOptions: {id: string, fill: number, thickness: float, classes: table}" }
                ]
            },
            line: {
                description: "Draws a line on the overlay",
                overloads: [
                    { params: [{ name: "x1", type: "number" }, { name: "y1", type: "number" }, { name: "x2", type: "number" }, { name: "y2", type: "number" }, { name: "color", type: "number" }], returns: "void", documentation: "Draws line with RGBA color (0xRRGGBBAA)" },
                    { params: [{ name: "x1", type: "number" }, { name: "y1", type: "number" }, { name: "x2", type: "number" }, { name: "y2", type: "number" }, { name: "color", type: "number" }, { name: "options", type: "table" }], returns: "void", documentation: "Draws line with options\n\nOptions: {id: string, thickness: float, classes: table}" }
                ]
            },
            remove_by_id: {
                description: "Removes a canvas element by its ID",
                overloads: [
                    { params: [{ name: "id", type: "string" }], returns: "void", documentation: "Removes the canvas element with the given ID" }
                ]
            },
            remove_by_classes: {
                description: "Removes canvas elements by class name(s)",
                overloads: [
                    { params: [{ name: "classes", type: "table" }], returns: "void", documentation: "Removes all canvas elements with the given classes\n\nExample: remove_by_classes({'ui', 'debug'})" }
                ]
            },
            clear: {
                description: "Clears all overlay graphics",
                overloads: [
                    { params: [], returns: "void", documentation: "Removes all drawn elements" }
                ]
            }
        },
        
        ocr: {
            fast: {
                recognize_text: {
                    description: "Performs fast OCR on the screen or buffer",
                    overloads: [
                        { params: [], returns: "OCRVector", documentation: "Recognizes text on entire screen" },
                        { params: [{ name: "options", type: "table" }], returns: "OCRVector", documentation: "Recognizes text in region\n\nOptions: {region: {x, y, w, h}}" },
                        { params: [{ name: "buffer", type: "PixelBuffer" }, { name: "options", type: "table" }], returns: "OCRVector", documentation: "Recognizes text in buffer region" }
                    ]
                }
            },
            accurate: {
                recognize_text: {
                    description: "Performs accurate OCR on the screen or buffer",
                    overloads: [
                        { params: [], returns: "OCRVector", documentation: "Recognizes text on entire screen" },
                        { params: [{ name: "options", type: "table" }], returns: "OCRVector", documentation: "Recognizes text in region\n\nOptions: {region: {x, y, w, h}}" },
                        { params: [{ name: "buffer", type: "PixelBuffer" }, { name: "options", type: "table" }], returns: "OCRVector", documentation: "Recognizes text in buffer region" }
                    ]
                }
            }
        }
    },
    
    mouse: {
        move: {
            description: "Moves mouse cursor",
            overloads: [
                { params: [{ name: "x", type: "number" }, { name: "y", type: "number" }], returns: "void", documentation: "Moves to absolute position" }
            ]
        },
        click: {
            description: "Clicks mouse button",
            overloads: [
                { params: [], returns: "void", documentation: "Left click at current position" },
                { params: [{ name: "button", type: "Button" }], returns: "void", documentation: "Clicks specified button\n\nExample: click(system.mouse.Button.RIGHT)" },
                { params: [{ name: "x", type: "number" }, { name: "y", type: "number" }], returns: "void", documentation: "Left click at position" },
                { params: [{ name: "x", type: "number" }, { name: "y", type: "number" }, { name: "button", type: "Button" }], returns: "void", documentation: "Clicks button at position" }
            ]
        },
        double_click: {
            description: "Double-clicks mouse button",
            overloads: [
                { params: [], returns: "void", documentation: "Left double-click at current position" },
                { params: [{ name: "button", type: "Button" }], returns: "void", documentation: "Double-clicks specified button" },
                { params: [{ name: "x", type: "number" }, { name: "y", type: "number" }], returns: "void", documentation: "Left double-click at position" },
                { params: [{ name: "x", type: "number" }, { name: "y", type: "number" }, { name: "button", type: "Button" }], returns: "void", documentation: "Double-clicks button at position" }
            ]
        },
        down: {
            description: "Presses mouse button down",
            overloads: [
                { params: [], returns: "void", documentation: "Left button down" },
                { params: [{ name: "button", type: "Button" }], returns: "void", documentation: "Specified button down" }
            ]
        },
        up: {
            description: "Releases mouse button",
            overloads: [
                { params: [], returns: "void", documentation: "Left button up" },
                { params: [{ name: "button", type: "Button" }], returns: "void", documentation: "Specified button up" }
            ]
        },
        drag: {
            description: "Drags from one position to another",
            overloads: [
                { params: [{ name: "x1", type: "number" }, { name: "y1", type: "number" }, { name: "x2", type: "number" }, { name: "y2", type: "number" }], returns: "void", documentation: "Drags with left button" },
                { params: [{ name: "x1", type: "number" }, { name: "y1", type: "number" }, { name: "x2", type: "number" }, { name: "y2", type: "number" }, { name: "button", type: "Button" }], returns: "void", documentation: "Drags with specified button" }
            ]
        },
        scroll: {
            description: "Scrolls mouse wheel",
            overloads: [
                { params: [{ name: "amount", type: "number" }], returns: "void", documentation: "Scrolls by amount (positive = up, negative = down)" }
            ]
        },
        get_position: {
            description: "Gets current cursor position",
            overloads: [
                { params: [], returns: "number, number", documentation: "Returns x, y" }
            ]
        },
        send: {
            description: "Sends low-level mouse event",
            overloads: [
                { params: [{ name: "button", type: "Button" }, { name: "event_type", type: "EventType" }], returns: "void", documentation: "Sends event at current position\n\nExample: send(Button.LEFT, EventType.DOWN)" },
                { params: [{ name: "button", type: "Button" }, { name: "event_type", type: "EventType" }, { name: "x", type: "number" }, { name: "y", type: "number" }], returns: "void", documentation: "Sends event at position\n\nExample: send(Button.LEFT, EventType.DOWN, 100, 200)" }
            ]
        }
    },
    
    keyboard: {
        tap: {
            description: "Taps a key",
            overloads: [
                { params: [{ name: "key", type: "string" }], returns: "void", documentation: "Taps key (e.g., 'a', 'enter', 'cmd')" }
            ]
        },
        press: {
            description: "Presses and releases a key (alias for tap)",
            overloads: [
                { params: [{ name: "key", type: "string" }], returns: "void", documentation: "Presses key (e.g., '1', '2', 'enter')" }
            ]
        },
        down: {
            description: "Presses key down",
            overloads: [
                { params: [{ name: "key", type: "string" }], returns: "void", documentation: "Holds key down" }
            ]
        },
        up: {
            description: "Releases key",
            overloads: [
                { params: [{ name: "key", type: "string" }], returns: "void", documentation: "Releases key" }
            ]
        },
        type: {
            description: "Types text",
            overloads: [
                { params: [{ name: "mode", type: "TypeMode" }, { name: "text", type: "string" }], returns: "void", documentation: "Types text with specified mode\n\nExample: type(TypeMode.SEQUENTIAL, 'hello')" },
                { params: [{ name: "mode", type: "TypeMode" }, { name: "text", type: "string" }, { name: "interval_ms", type: "integer" }], returns: "void", documentation: "Types with mode and custom interval (SEQUENTIAL only)\n\nExample: type(TypeMode.SEQUENTIAL, 'hello', 50)" }
            ]
        },
        send: {
            description: "Sends low-level keyboard event",
            overloads: [
                { params: [{ name: "event_type", type: "EventType" }, { name: "key", type: "string" }], returns: "void", documentation: "Example: send(EventType.DOWN, 'shift')" }
            ]
        }
    },
    
    opencv: {
        load_from_file: {
            description: "Loads image from file",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "Mat", documentation: "Returns OpenCV Mat object" }
            ]
        },
        load_image: {
            description: "Loads image from file (alias for load_from_file)",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "Mat", documentation: "Returns OpenCV Mat object" }
            ]
        },
        save_image: {
            description: "Saves Mat to file",
            overloads: [
                { params: [{ name: "mat", type: "Mat" }, { name: "path", type: "string" }], returns: "void", documentation: "Saves image as PNG/JPG" }
            ]
        },
        cvtColor: {
            description: "Converts color space",
            overloads: [
                { params: [{ name: "src", type: "Mat" }, { name: "code", type: "number" }], returns: "Mat", documentation: "Example: cvtColor(img, cv.COLOR_BGR2GRAY)" }
            ]
        },
        threshold: {
            description: "Applies threshold",
            overloads: [
                { params: [{ name: "src", type: "Mat" }, { name: "thresh", type: "number" }, { name: "maxval", type: "number" }, { name: "type", type: "number" }], returns: "Mat", documentation: "Binary threshold" }
            ]
        },
        findContours: {
            description: "Finds contours in binary image",
            overloads: [
                { params: [{ name: "image", type: "Mat" }, { name: "mode", type: "number" }, { name: "method", type: "number" }], returns: "table", documentation: "Returns array of contours" }
            ]
        }
    },

    fs: {
        open: {
            description: "Opens a file handle",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "FileHandle", documentation: "Opens for read mode ('r') by default" },
                { params: [{ name: "path", type: "string" }, { name: "mode", type: "string" }], returns: "FileHandle", documentation: "Modes: r, w, a, r+, w+, a+ (with optional b)" }
            ]
        },
        read_all: {
            description: "Reads a full file into memory",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "string", documentation: "Reads entire file contents" }
            ]
        },
        write_all: {
            description: "Writes a full file",
            overloads: [
                { params: [{ name: "path", type: "string" }, { name: "data", type: "string" }], returns: "boolean", documentation: "Creates/truncates then writes data" }
            ]
        },
        append: {
            description: "Appends data to a file",
            overloads: [
                { params: [{ name: "path", type: "string" }, { name: "data", type: "string" }], returns: "boolean", documentation: "Appends data to file end" }
            ]
        },
        exists: {
            description: "Checks if a path exists",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "boolean", documentation: "Returns true when file/dir exists" }
            ]
        },
        stat: {
            description: "Returns metadata for a path",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "table", documentation: "Returns {path, exists, is_file, is_dir, size}" }
            ]
        },
        list: {
            description: "Lists manifest-allowed files under path",
            overloads: [
                { params: [], returns: "table", documentation: "Lists from project root" },
                { params: [{ name: "path", type: "string" }], returns: "table", documentation: "Lists paths under directory" }
            ]
        },
        remove: {
            description: "Removes file or directory tree",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "boolean", documentation: "Deletes path if allowed" }
            ]
        },
        mkdir: {
            description: "Creates directory",
            overloads: [
                { params: [{ name: "path", type: "string" }], returns: "boolean", documentation: "Creates path recursively" },
                { params: [{ name: "path", type: "string" }, { name: "recursive", type: "boolean" }], returns: "boolean", documentation: "Creates with recursive flag" }
            ]
        },
        rename: {
            description: "Renames/moves a file",
            overloads: [
                { params: [{ name: "src", type: "string" }, { name: "dst", type: "string" }], returns: "boolean", documentation: "Moves src to dst" }
            ]
        }
    }
};

function createCompletionItems(prefix, apiObj, range, basePath = '') {
    const suggestions = [];
    const fullPath = basePath ? `${basePath}.${prefix}` : prefix;
    
    for (const key in apiObj) {
        if (key.startsWith('_')) continue;
        
        const value = apiObj[key];
        if (value && value.overloads) {
            // It's a function - create a suggestion for each overload
            value.overloads.forEach((overload, index) => {
                const params = overload.params.map((p, i) => `\${${i + 1}:${p.name}}`).join(', ');
                const snippet = params ? `${key}(${params})` : `${key}()`;
                const paramSignature = overload.params.map(p => `${p.name}: ${p.type}`).join(', ');
                
                // Format documentation with markdown
                const docMarkdown = {
                    value: `**${value.description}**\n\n${overload.documentation}\n\n*Returns:* \`${overload.returns}\``,
                    isTrusted: true
                };
                
                suggestions.push({
                    label: value.overloads.length > 1 ? `${key}(${paramSignature})` : key,
                    kind: monaco.languages.CompletionItemKind.Function,
                    insertText: snippet,
                    insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
                    documentation: docMarkdown,
                    detail: value.overloads.length > 1 ? `Overload ${index + 1}/${value.overloads.length}` : `(${paramSignature}) → ${overload.returns}`,
                    range,
                    sortText: `1_${key}_${index}`, // Functions come first
                    commitCharacters: ['('] // Auto-commit on '('
                });
            });
        } else if (typeof value === 'object') {
            // It's a submodule
            suggestions.push({
                label: key,
                kind: monaco.languages.CompletionItemKind.Module,
                insertText: key,
                documentation: {
                    value: `**Module:** ${fullPath}.${key}`,
                    isTrusted: true
                },
                range,
                sortText: `0_${key}`, // Modules come before functions
                commitCharacters: ['.'] // Auto-commit on '.'
            });
        }
    }
    
    return suggestions;
}

export function registerLuaAPI(monacoInstance) {
    monaco = monacoInstance;
    
    // Register completion provider
    monaco.languages.registerCompletionItemProvider('lua', {
        triggerCharacters: ['.'],
        provideCompletionItems: (model, position) => {
            const textUntilPosition = model.getValueInRange({
                startLineNumber: 1,
                startColumn: 1,
                endLineNumber: position.lineNumber,
                endColumn: position.column
            });
            
            // Extract the identifier chain before cursor, including partial identifiers
            // Matches: system, system.m, system.mouse, system.mouse.cl, etc.
            const match = textUntilPosition.match(/(\w+(?:\.\w*)*)$/);
            if (!match) {
                return { suggestions: [] };
            }
            
            const fullText = match[1];
            const parts = fullText.split('.');
            
            // Calculate the range to replace - only replace the partial word after the last dot
            const lastPart = parts[parts.length - 1];
            const range = {
                startLineNumber: position.lineNumber,
                endLineNumber: position.lineNumber,
                startColumn: position.column - lastPart.length,
                endColumn: position.column
            };
            
            // Check if we're completing after a function call (e.g., find_color().x)
            // Match patterns like: system.screen.find_color(...).
            const functionCallMatch = textUntilPosition.match(/(\w+(?:\.\w+)*)\.(\w+)\([^)]*\)\.(\w*)$/);
            if (functionCallMatch) {
                const [, pathStr, funcName, partialProp] = functionCallMatch;
                const path = pathStr.split('.');
                
                // Navigate to the function in the API
                let api = luaAPI;
                if (path.length >= 2 && path[0] === 'system') {
                    for (let i = 1; i < path.length; i++) {
                        api = api[path[i]];
                        if (!api) return { suggestions: [] };
                    }
                } else {
                    for (const part of path) {
                        api = api[part];
                        if (!api) return { suggestions: [] };
                    }
                }
                
                const func = api[funcName];
                if (func && func.overloads && func.overloads.length > 0) {
                    // Get the return type from the first overload
                    const returnType = func.overloads[0].returns;
                    
                    // If it returns a table, suggest table properties
                    if (returnType === 'table' || returnType === 'table?') {
                        const tableDef = typeDefinitions['table'];
                        const suggestions = [];
                        for (const prop in tableDef) {
                            suggestions.push({
                                label: prop,
                                kind: monaco.languages.CompletionItemKind.Property,
                                insertText: prop,
                                documentation: tableDef[prop].description,
                                detail: tableDef[prop].type,
                                range,
                                sortText: `2_${prop}`
                            });
                        }
                        return { suggestions };
                    }
                }
            }
            
            // Validate the path up to the second-to-last part
            // For "system.a.", we need to validate that "system" exists
            // For "system.mouse.b.", we need to validate that "system.mouse" exists
            if (parts.length >= 2) {
                let api = luaAPI;
                
                // Navigate through all parts except the last one (which is being typed)
                for (let i = 0; i < parts.length - 1; i++) {
                    const part = parts[i];
                    
                    // Special handling for 'system' - skip it in navigation
                    if (i === 0 && part === 'system') {
                        continue;
                    }
                    
                    // For system.X, check if X exists in luaAPI
                    if (i === 1 && parts[0] === 'system') {
                        api = luaAPI[part];
                    } else {
                        api = api[part];
                    }
                    
                    // If the path is invalid, return no suggestions
                    if (!api) {
                        return { suggestions: [] };
                    }
                }
            }
            
            // Handle canvas submodule
            if (parts.length >= 4 && parts[0] === 'system' && parts[1] === 'screen' && parts[2] === 'canvas') {
                return { suggestions: createCompletionItems('canvas', luaAPI.screen.canvas, range, 'system.screen') };
            }
            
            // Handle OCR fast/accurate submodules
            if (parts.length >= 5 && parts[0] === 'system' && parts[1] === 'screen' && parts[2] === 'ocr' && parts[3] === 'fast') {
                return { suggestions: createCompletionItems('fast', luaAPI.screen.ocr.fast, range, 'system.screen.ocr') };
            }
            
            if (parts.length >= 5 && parts[0] === 'system' && parts[1] === 'screen' && parts[2] === 'ocr' && parts[3] === 'accurate') {
                return { suggestions: createCompletionItems('accurate', luaAPI.screen.ocr.accurate, range, 'system.screen.ocr') };
            }
            
            // Handle OCR module
            if (parts.length >= 4 && parts[0] === 'system' && parts[1] === 'screen' && parts[2] === 'ocr') {
                return { suggestions: [
                    { label: 'fast', kind: monaco.languages.CompletionItemKind.Module, insertText: 'fast', documentation: 'Fast OCR mode', range, sortText: '0_fast', commitCharacters: ['.'] },
                    { label: 'accurate', kind: monaco.languages.CompletionItemKind.Module, insertText: 'accurate', documentation: 'Accurate OCR mode', range, sortText: '0_accurate', commitCharacters: ['.'] }
                ]};
            }
            
            // Handle screen module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'screen') {
                const suggestions = createCompletionItems('screen', luaAPI.screen, range, 'system');
                suggestions.push(
                    { label: 'canvas', kind: monaco.languages.CompletionItemKind.Module, insertText: 'canvas', documentation: 'Drawing overlay functions', range, sortText: '0_canvas', commitCharacters: ['.'] },
                    { label: 'ocr', kind: monaco.languages.CompletionItemKind.Module, insertText: 'ocr', documentation: 'OCR text recognition', range, sortText: '0_ocr', commitCharacters: ['.'] },
                    { label: 'width', kind: monaco.languages.CompletionItemKind.Property, insertText: 'width', documentation: 'Screen width', range, sortText: '2_width' },
                    { label: 'height', kind: monaco.languages.CompletionItemKind.Property, insertText: 'height', documentation: 'Screen height', range, sortText: '2_height' },
                    { label: 'ready', kind: monaco.languages.CompletionItemKind.Property, insertText: 'ready', documentation: 'Capture ready status', range, sortText: '2_ready' },
                    { label: 'stride', kind: monaco.languages.CompletionItemKind.Property, insertText: 'stride', documentation: 'Buffer stride', range, sortText: '2_stride' },
                    { label: 'index', kind: monaco.languages.CompletionItemKind.Property, insertText: 'index', documentation: 'Frame index', range, sortText: '2_index' },
                    { label: 'timestamp', kind: monaco.languages.CompletionItemKind.Property, insertText: 'timestamp', documentation: 'Frame timestamp', range, sortText: '2_timestamp' }
                );
                return { suggestions };
            }
            
            // Handle mouse module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'mouse') {
                const suggestions = createCompletionItems('mouse', luaAPI.mouse, range, 'system');
                suggestions.push(
                    { label: 'Button', kind: monaco.languages.CompletionItemKind.Enum, insertText: 'Button', documentation: 'Mouse button constants', range, sortText: '0_Button', commitCharacters: ['.'] },
                    { label: 'EventType', kind: monaco.languages.CompletionItemKind.Enum, insertText: 'EventType', documentation: 'Mouse event types', range, sortText: '0_EventType', commitCharacters: ['.'] }
                );
                return { suggestions };
            }
            
            // Handle keyboard module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'keyboard') {
                const suggestions = createCompletionItems('keyboard', luaAPI.keyboard, range, 'system');
                suggestions.push(
                    { label: 'EventType', kind: monaco.languages.CompletionItemKind.Enum, insertText: 'EventType', documentation: 'Keyboard event types', range, sortText: '0_EventType', commitCharacters: ['.'] },
                    { label: 'TypeMode', kind: monaco.languages.CompletionItemKind.Enum, insertText: 'TypeMode', documentation: 'Keyboard type modes', range, sortText: '0_TypeMode', commitCharacters: ['.'] }
                );
                return { suggestions };
            }
            
            // Handle opencv module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'opencv') {
                return { suggestions: createCompletionItems('opencv', luaAPI.opencv, range, 'system') };
            }

            // Handle ui module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'ui') {
                return { suggestions: createCompletionItems('ui', luaAPI.ui, range, 'system') };
            }

            // Handle fs module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'fs') {
                return { suggestions: createCompletionItems('fs', luaAPI.fs, range, 'system') };
            }
            
            // Handle stats module
            if (parts.length >= 3 && parts[0] === 'system' && parts[1] === 'stats') {
                return { suggestions: createCompletionItems('stats', luaAPI.stats, range, 'system') };
            }
            
            // Handle system module (after typing "system." or just "system")
            if (parts.length >= 2 && parts[0] === 'system') {
                const suggestions = createCompletionItems('system', luaAPI.system, range);
                suggestions.push(
                    { label: 'screen', kind: monaco.languages.CompletionItemKind.Module, insertText: 'screen', documentation: 'Screen capture and analysis', range, sortText: '0_screen', commitCharacters: ['.'] },
                    { label: 'mouse', kind: monaco.languages.CompletionItemKind.Module, insertText: 'mouse', documentation: 'Mouse control', range, sortText: '0_mouse', commitCharacters: ['.'] },
                    { label: 'keyboard', kind: monaco.languages.CompletionItemKind.Module, insertText: 'keyboard', documentation: 'Keyboard control', range, sortText: '0_keyboard', commitCharacters: ['.'] },
                    { label: 'opencv', kind: monaco.languages.CompletionItemKind.Module, insertText: 'opencv', documentation: 'OpenCV functions', range, sortText: '0_opencv', commitCharacters: ['.'] },
                    { label: 'stats', kind: monaco.languages.CompletionItemKind.Module, insertText: 'stats', documentation: 'System statistics', range, sortText: '0_stats', commitCharacters: ['.'] },
                    { label: 'fs', kind: monaco.languages.CompletionItemKind.Module, insertText: 'fs', documentation: 'Manifest-aware filesystem API', range, sortText: '0_fs', commitCharacters: ['.'] },
                    { label: 'ui', kind: monaco.languages.CompletionItemKind.Module, insertText: 'ui', documentation: 'Macro UI windows and events', range, sortText: '0_ui', commitCharacters: ['.'] }
                );
                return { suggestions };
            }
            
            // Top-level suggestions (just "system")
            if (parts.length === 1) {
                const suggestions = [
                    { 
                        label: 'system', 
                        kind: monaco.languages.CompletionItemKind.Module, 
                        insertText: 'system', 
                        documentation: 'Main system API', 
                        range,
                        sortText: '0_system'
                    }
                ];
                
                // Add common Lua standard library suggestions
                if (parts[0] === 'm' || parts[0] === 'ma' || parts[0] === 'mat' || parts[0] === 'math' || parts[0] === '') {
                    suggestions.push({
                        label: 'math',
                        kind: monaco.languages.CompletionItemKind.Module,
                        insertText: 'math',
                        documentation: 'Lua math library',
                        range,
                        sortText: '1_math'
                    });
                }
                
                if (parts[0] === 's' || parts[0] === 'st' || parts[0] === 'str' || parts[0] === 'stri' || parts[0] === 'strin' || parts[0] === 'string' || parts[0] === '') {
                    suggestions.push({
                        label: 'string',
                        kind: monaco.languages.CompletionItemKind.Module,
                        insertText: 'string',
                        documentation: 'Lua string library',
                        range,
                        sortText: '1_string'
                    });
                }
                
                if (parts[0] === 't' || parts[0] === 'ta' || parts[0] === 'tab' || parts[0] === 'tabl' || parts[0] === 'table' || parts[0] === '') {
                    suggestions.push({
                        label: 'table',
                        kind: monaco.languages.CompletionItemKind.Module,
                        insertText: 'table',
                        documentation: 'Lua table library',
                        range,
                        sortText: '1_table'
                    });
                }
                
                return { suggestions };
            }
            
            return { suggestions: [] };
        }
    });
    
    // Register signature help provider (parameter hints)
    monaco.languages.registerSignatureHelpProvider('lua', {
        signatureHelpTriggerCharacters: ['(', ','],
        provideSignatureHelp: (model, position) => {
            const textUntilPosition = model.getValueInRange({
                startLineNumber: 1,
                startColumn: 1,
                endLineNumber: position.lineNumber,
                endColumn: position.column
            });
            
            // Find the function call we're inside
            // Match patterns like: system.mouse.click(
            const funcMatch = textUntilPosition.match(/(\w+(?:\.\w+)*)\.(\w+)\([^)]*$/);
            if (!funcMatch) return null;
            
            const [, pathStr, funcName] = funcMatch;
            const path = pathStr.split('.');
            
            // Navigate through the API structure
            let api = luaAPI;
            if (path.length >= 2 && path[0] === 'system') {
                for (let i = 1; i < path.length; i++) {
                    api = api[path[i]];
                    if (!api) return null;
                }
            } else {
                for (const part of path) {
                    api = api[part];
                    if (!api) return null;
                }
            }
            
            const func = api[funcName];
            if (!func || !func.overloads) return null;
            
            // Count commas to determine which parameter we're on
            const currentCallText = textUntilPosition.match(/\([^)]*$/)[0];
            const commaCount = (currentCallText.match(/,/g) || []).length;
            
            const signatures = func.overloads.map(overload => {
                const params = overload.params.map(p => ({
                    label: `${p.name}: ${p.type}`,
                    documentation: p.type
                }));
                
                return {
                    label: `${funcName}(${overload.params.map(p => `${p.name}: ${p.type}`).join(', ')}) → ${overload.returns}`,
                    documentation: overload.documentation,
                    parameters: params
                };
            });
            
            return {
                signatures,
                activeSignature: 0,
                activeParameter: Math.min(commaCount, signatures[0].parameters.length - 1)
            };
        }
    });
    
    // Register hover provider with improved function detection
    monaco.languages.registerHoverProvider('lua', {
        provideHover: (model, position) => {
            const word = model.getWordAtPosition(position);
            if (!word) return null;
            
            const lineContent = model.getLineContent(position.lineNumber);
            
            // Get text before and including the current word
            const textBeforeAndIncluding = lineContent.substring(0, word.endColumn - 1);
            
            // Handle properties like system.screen.width
            const propMatch = textBeforeAndIncluding.match(/system\.screen\.(width|height|stride|index|ready|timestamp)$/);
            if (propMatch) {
                const prop = propMatch[1];
                const propDocs = {
                    width: "**system.screen.width** (number)\n\nThe width of the captured buffer",
                    height: "**system.screen.height** (number)\n\nThe height of the captured buffer",
                    stride: "**system.screen.stride** (number)\n\nThe byte-stride (width * 4) of the buffer",
                    index: "**system.screen.index** (number)\n\nThe current frame index (increments every frame)",
                    ready: "**system.screen.ready** (boolean)\n\nReturns true if capture is active and first frame received",
                    timestamp: "**system.screen.timestamp** (number)\n\nThe current frame timestamp"
                };
                return { contents: [{ value: propDocs[prop] || "" }] };
            }
            
            // Handle constants
            if (textBeforeAndIncluding.match(/system\.mouse\.Button\.(LEFT|RIGHT|MIDDLE)$/)) {
                return { contents: [{ value: "**Mouse Button Constant**\n\nUse with mouse.click() and mouse.send()" }] };
            }
            if (textBeforeAndIncluding.match(/system\.keyboard\.TypeMode\.(SEQUENTIAL|STRING)$/)) {
                return { contents: [{ value: "**Keyboard Type Mode**\n\nSEQUENTIAL: Human-like typing\nSTRING: Instant input" }] };
            }
            
            // Enhanced function detection - extract the full path including the function name
            // This works whether hovering on the function name or the opening parenthesis
            const funcMatch = textBeforeAndIncluding.match(/(\w+(?:\.\w+)*)\.(\w+)$/);
            
            if (funcMatch) {
                const [, pathStr, funcName] = funcMatch;
                const path = pathStr.split('.');
                
                // Navigate through the API structure
                let api = luaAPI;
                
                // Special handling for system.module.function pattern
                if (path.length >= 2 && path[0] === 'system') {
                    // For system.mouse.click, we want luaAPI.mouse.click
                    // For system.screen.canvas.clear, we want luaAPI.screen.canvas.clear
                    // Skip 'system' and navigate the rest
                    for (let i = 1; i < path.length; i++) {
                        api = api[path[i]];
                        if (!api) return null;
                    }
                } else {
                    // For other patterns, navigate normally
                    for (const part of path) {
                        api = api[part];
                        if (!api) return null;
                    }
                }
                
                const func = api[funcName];
                if (func && func.overloads) {
                    const overloadDocs = func.overloads.map((o, i) => {
                        const params = o.params.map(p => `${p.name}: ${p.type}`).join(', ');
                        return `**Overload ${i + 1}:** ${funcName}(${params}) → \`${o.returns}\`\n\n${o.documentation}`;
                    }).join('\n\n---\n\n');
                    
                    return {
                        contents: [
                            { value: `**${pathStr}.${funcName}**` },
                            { value: func.description },
                            { value: overloadDocs }
                        ]
                    };
                }
            }
            
            return null;
        }
    });
    
    // Register definition provider (cmd+click to go to definition)
    monaco.languages.registerDefinitionProvider('lua', {
        provideDefinition: (model, position) => {
            const word = model.getWordAtPosition(position);
            if (!word) return null;
            
            const functionName = word.word;
            const lineContent = model.getLineContent(position.lineNumber);
            const textBeforeAndIncluding = lineContent.substring(0, word.endColumn - 1);
            
            // First, try to find user-defined function declarations
            const fullText = model.getValue();
            const lines = fullText.split('\n');
            
            // Search for function definitions: "function name(" or "local function name("
            const functionDefPattern = new RegExp(`^\\s*(local\\s+)?function\\s+${functionName}\\s*\\(`, 'm');
            for (let i = 0; i < lines.length; i++) {
                if (functionDefPattern.test(lines[i])) {
                    // Found the function definition
                    const functionIndex = lines[i].indexOf('function');
                    const nameIndex = lines[i].indexOf(functionName, functionIndex);
                    return {
                        uri: model.uri,
                        range: {
                            startLineNumber: i + 1,
                            startColumn: nameIndex + 1,
                            endLineNumber: i + 1,
                            endColumn: nameIndex + functionName.length + 1
                        }
                    };
                }
            }
            
            // Also check for variable assignments: "name = function(" or "local name = function("
            const varFunctionPattern = new RegExp(`^\\s*(local\\s+)?${functionName}\\s*=\\s*function\\s*\\(`, 'm');
            for (let i = 0; i < lines.length; i++) {
                if (varFunctionPattern.test(lines[i])) {
                    const nameIndex = lines[i].indexOf(functionName);
                    return {
                        uri: model.uri,
                        range: {
                            startLineNumber: i + 1,
                            startColumn: nameIndex + 1,
                            endLineNumber: i + 1,
                            endColumn: nameIndex + functionName.length + 1
                        }
                    };
                }
            }
            
            // If not a user function, check if it's an API function
            // Match function calls or property accesses like system.mouse.click
            const apiMatch = textBeforeAndIncluding.match(/(\w+(?:\.\w+)*)\.(\w+)$/);
            if (!apiMatch) return null;
            
            const [, pathStr, funcName] = apiMatch;
            const path = pathStr.split('.');
            
            // Navigate through the API structure
            let api = luaAPI;
            if (path.length >= 2 && path[0] === 'system') {
                for (let i = 1; i < path.length; i++) {
                    api = api[path[i]];
                    if (!api) return null;
                }
            } else {
                for (const part of path) {
                    api = api[part];
                    if (!api) return null;
                }
            }
            
            const func = api[funcName];
            if (func && func.overloads) {
                // Generate documentation content
                const docLines = [
                    `-- ${pathStr}.${funcName}`,
                    `-- ${func.description}`,
                    `--`
                ];
                
                func.overloads.forEach((overload, i) => {
                    const params = overload.params.map(p => `${p.name}: ${p.type}`).join(', ');
                    docLines.push(`-- Overload ${i + 1}: ${funcName}(${params}) → ${overload.returns}`);
                    docLines.push(`--   ${overload.documentation.replace(/\n/g, '\n--   ')}`);
                    docLines.push(`--`);
                });
                
                // Create a virtual URI for the documentation
                const docUri = monaco.Uri.parse(`inmemory://api-docs/${pathStr}.${funcName}.lua`);
                
                // Check if model already exists
                let docModel = monaco.editor.getModel(docUri);
                if (!docModel) {
                    docModel = monaco.editor.createModel(docLines.join('\n'), 'lua', docUri);
                }
                
                return {
                    uri: docUri,
                    range: {
                        startLineNumber: 1,
                        startColumn: 1,
                        endLineNumber: 1,
                        endColumn: 1
                    }
                };
            }
            
            return null;
        }
    });
}