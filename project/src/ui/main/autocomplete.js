let monaco = null;

const fn = (description, overloads) => ({ description, overloads });
const mod = (description, members = {}) => ({ _moduleDescription: description, ...members });
const constants = (description, values) => ({ _constantsDescription: description, _constants: values });

const luaApiTree = {
    system: mod('Main runtime API', {
        on_key: fn('Register a keybind callback', [
            {
                params: [{ name: 'combo', type: 'string' }, { name: 'callback', type: 'function' }],
                returns: 'void',
                documentation: "Registers a keybind callback. Example: system.on_key('cmd+space', function() end)"
            },
            {
                params: [{ name: 'combo', type: 'string' }, { name: 'options', type: '{swallow?:boolean}' }, { name: 'callback', type: 'function' }],
                returns: 'void',
                documentation: "Registers a keybind callback with options."
            }
        ]),
        remove_keybind: fn('Remove a previously registered keybind', [
            { params: [{ name: 'combo', type: 'string' }], returns: 'void', documentation: 'Unregisters the keybind.' }
        ]),
        wait: fn('Pause execution', [
            { params: [{ name: 'time', type: 'number' }], returns: 'void', documentation: 'Waits in seconds.' },
            { params: [{ name: 'time', type: 'number' }, { name: 'unit', type: "'s'|'ms'|'us'" }], returns: 'void', documentation: 'Waits using the given time unit.' }
        ]),
        get_time: fn('Read monotonic runtime clock', [
            { params: [], returns: 'number', documentation: 'Returns seconds.' },
            { params: [{ name: 'unit', type: "'s'|'ms'|'us'" }], returns: 'number', documentation: 'Returns time in the requested unit.' }
        ]),
        set_timeout: fn('Schedule a one-shot callback', [
            { params: [{ name: 'callback', type: 'function' }, { name: 'time', type: 'number' }], returns: 'integer', documentation: 'Schedules callback after delay (seconds).' },
            { params: [{ name: 'callback', type: 'function' }, { name: 'time', type: 'number' }, { name: 'unit', type: "'s'|'ms'|'us'" }], returns: 'integer', documentation: 'Schedules callback after delay in given unit.' }
        ]),
        clear_timeout: fn('Cancel timeout timer', [
            { params: [{ name: 'timer_id', type: 'integer' }], returns: 'void', documentation: 'Cancels a timeout created by set_timeout.' }
        ]),
        set_interval: fn('Schedule a repeating callback', [
            { params: [{ name: 'callback', type: 'function' }, { name: 'time', type: 'number' }], returns: 'integer', documentation: 'Runs callback periodically (seconds).' },
            { params: [{ name: 'callback', type: 'function' }, { name: 'time', type: 'number' }, { name: 'unit', type: "'s'|'ms'|'us'" }], returns: 'integer', documentation: 'Runs callback periodically in given unit.' }
        ]),
        clear_interval: fn('Cancel interval timer', [
            { params: [{ name: 'timer_id', type: 'integer' }], returns: 'void', documentation: 'Cancels an interval created by set_interval.' }
        ]),
        save_frame: fn('Save a frame buffer to file', [
            { params: [{ name: 'buffer', type: 'PixelBuffer|PixelBufferReadOnly' }, { name: 'filename', type: 'string' }], returns: 'boolean', documentation: 'Writes image using extension format (png/jpg/jpeg/bmp/tiff/webp/ppm).' }
        ]),
        exit: fn('Terminate macro runner', [
            { params: [], returns: 'void', documentation: 'Exit with code 0.' },
            { params: [{ name: 'exit_code', type: 'integer' }], returns: 'void', documentation: 'Exit with explicit code.' }
        ]),
        _create_task: fn('Create internal background coroutine task', [
            { params: [{ name: 'callback', type: 'function' }], returns: 'void', documentation: 'Advanced API used by bootstrap/task scheduler.' }
        ]),
        stats: mod('Runtime resource stats', {
            get_info: fn('Read CPU/RAM/VMEM usage', [
                { params: [], returns: '{cpu:number,ram:number,vmem:number}', documentation: 'Default RAM unit is GB.' },
                { params: [{ name: 'unit', type: "'b'|'kb'|'mb'|'gb'" }], returns: '{cpu:number,ram:number,vmem:number}', documentation: 'Returns RAM/VMEM in requested unit.' }
            ])
        }),
        permissions: mod('Runtime permission state', {
            get: fn('Read permission grants', [
                { params: [], returns: 'table<string, boolean>', documentation: 'Returns all available permission grants.' },
                { params: [{ name: 'name', type: 'string' }], returns: 'boolean', documentation: 'Returns grant state for one permission key.' }
            ])
        }),
        ui: mod('Macro UI windows and events', {
            open: fn('Open UI window from HTML path', [
                { params: [{ name: 'path', type: 'string' }], returns: 'UIWindowHandle', documentation: 'Opens a UI window and returns a handle.' },
                { params: [{ name: 'path', type: 'string' }, { name: 'options', type: '{title?:string,x?:number,y?:number,width?:number,height?:number,w?:number,h?:number}' }], returns: 'UIWindowHandle', documentation: 'Opens with custom window options.' }
            ]),
            close: fn('Close a UI window', [
                { params: [], returns: 'void', documentation: 'Closes the most recently opened UI window.' },
                { params: [{ name: 'window_id', type: 'integer' }], returns: 'void', documentation: 'Closes a specific UI window by id.' }
            ]),
            run_js: fn('Execute JavaScript in a UI window', [
                { params: [{ name: 'js', type: 'string' }], returns: 'void', documentation: 'Runs JS in the last opened UI window.' },
                { params: [{ name: 'js', type: 'string' }, { name: 'window_id', type: 'integer' }], returns: 'void', documentation: 'Runs JS in a specific window id.' }
            ]),
            on: fn('Register UI event callback', [
                { params: [{ name: 'event', type: 'string' }, { name: 'callback', type: 'function(payload, window_id)' }], returns: 'void', documentation: 'Registers callback for events emitted by UI bridge.' }
            ]),
            off: fn('Unregister UI event callback', [
                { params: [{ name: 'event', type: 'string' }], returns: 'void', documentation: 'Removes callback for event.' }
            ])
        }),
        screen: mod('Screen capture and analysis', {
            wait_until_ready: fn('Wait until first captured frame is available', [
                { params: [], returns: 'void', documentation: 'Blocks until capture state is ready.' }
            ]),
            get_dimensions: fn('Get current screen dimensions', [
                { params: [], returns: 'number, number', documentation: 'Returns width and height.' }
            ]),
            get_current_timestamp: fn('Get high-resolution current timestamp', [
                { params: [], returns: 'number', documentation: 'Returns microseconds timestamp.' }
            ]),
            begin_capture: fn('Start capture stream', [
                { params: [], returns: 'void', documentation: 'Starts capture with defaults (fps=60, main display).' },
                { params: [{ name: 'options', type: '{fps?:integer,region?:{x,y,w,h},display_id?:integer}' }], returns: 'void', documentation: 'Starts capture with options table.' },
                { params: [{ name: 'callback', type: 'function' }], returns: 'void', documentation: 'Starts capture and calls callback on new frames.' },
                { params: [{ name: 'callback', type: 'function' }, { name: 'options', type: '{fps?:integer,region?:{x,y,w,h},display_id?:integer}' }], returns: 'void', documentation: 'Callback + options form.' }
            ]),
            stop_capture: fn('Stop active capture stream', [
                { params: [], returns: 'void', documentation: 'Stops capture and releases SHM mapping.' }
            ]),
            get_displays: fn('List active displays', [
                { params: [], returns: 'Display[]', documentation: 'Returns display objects with bounds/scale/refresh_rate.' }
            ]),
            get_main_display: fn('Get main display metadata', [
                { params: [], returns: 'Display', documentation: 'Returns active main display descriptor.' }
            ]),
            get_display: fn('Get one display metadata by id', [
                { params: [{ name: 'id', type: 'integer' }], returns: 'Display', documentation: 'Returns display descriptor for id.' }
            ]),
            get_next_frame: fn('Wait for next frame index', [
                { params: [], returns: 'boolean, string?', documentation: 'Waits for next frame with default timeout.' },
                { params: [{ name: 'timeout_ms', type: 'number' }], returns: 'boolean, string?', documentation: "Returns success plus optional error ('timeout')." }
            ]),
            find_color: fn('Find first matching color in current buffer', [
                { params: [{ name: 'r', type: 'number' }, { name: 'g', type: 'number' }, { name: 'b', type: 'number' }], returns: '{x:number,y:number}?', documentation: 'Exact search on full frame.' },
                { params: [{ name: 'r', type: 'number' }, { name: 'g', type: 'number' }, { name: 'b', type: 'number' }, { name: 'tolerance', type: 'number' }], returns: '{x:number,y:number}?', documentation: 'Fuzzy search using tolerance.' },
                { params: [{ name: 'r', type: 'number' }, { name: 'g', type: 'number' }, { name: 'b', type: 'number' }, { name: 'tolerance', type: 'number' }, { name: 'rect', type: '{x:number,y:number,w:number,h:number}' }], returns: '{x:number,y:number}?', documentation: 'Constrain search region.' },
                { params: [{ name: 'r', type: 'number' }, { name: 'g', type: 'number' }, { name: 'b', type: 'number' }, { name: 'tolerance', type: 'number' }, { name: 'rect', type: '{x:number,y:number,w:number,h:number}' }, { name: 'reverse', type: 'boolean' }, { name: 'reverse_vertical', type: 'boolean' }], returns: '{x:number,y:number}?', documentation: 'Control scan direction.' }
            ]),
            save_frame: fn('Save current screen frame to image', [
                { params: [{ name: 'filename', type: 'string' }], returns: 'boolean', documentation: 'Saves latest frame to file path.' }
            ]),
            save_screenshot: fn('Alias for save_frame', [
                { params: [{ name: 'filename', type: 'string' }], returns: 'boolean', documentation: 'Alias of system.screen.save_frame.' }
            ]),
            canvas: mod('Overlay drawing commands', {
                rect: fn('Draw rectangle', [
                    { params: [{ name: 'rect', type: '{x:number,y:number,w:number,h:number}' }, { name: 'color', type: 'number' }], returns: 'void', documentation: 'Draws outlined rectangle (RGBA packed color).' },
                    { params: [{ name: 'rect', type: '{x:number,y:number,w:number,h:number}' }, { name: 'color', type: 'number' }, { name: 'options', type: '{id?:string,fill?:number,thickness?:number,classes?:string[]}' }], returns: 'void', documentation: 'Draws rectangle with options.' }
                ]),
                text: fn('Draw text', [
                    { params: [{ name: 'text', type: 'string' }, { name: 'x', type: 'number' }, { name: 'y', type: 'number' }, { name: 'color', type: 'number' }], returns: 'void', documentation: 'Draws text at coordinates.' },
                    { params: [{ name: 'text', type: 'string' }, { name: 'x', type: 'number' }, { name: 'y', type: 'number' }, { name: 'color', type: 'number' }, { name: 'options', type: '{id?:string,thickness?:number,classes?:string[]}' }], returns: 'void', documentation: 'Draws text with style options.' }
                ]),
                line: fn('Draw line', [
                    { params: [{ name: 'x1', type: 'number' }, { name: 'y1', type: 'number' }, { name: 'x2', type: 'number' }, { name: 'y2', type: 'number' }, { name: 'color', type: 'number' }], returns: 'void', documentation: 'Draws a line segment.' },
                    { params: [{ name: 'x1', type: 'number' }, { name: 'y1', type: 'number' }, { name: 'x2', type: 'number' }, { name: 'y2', type: 'number' }, { name: 'color', type: 'number' }, { name: 'options', type: '{id?:string,thickness?:number,classes?:string[]}' }], returns: 'void', documentation: 'Draws a styled line segment.' }
                ]),
                remove_by_id: fn('Remove draw command by id', [
                    { params: [{ name: 'id', type: 'string' }], returns: 'void', documentation: 'Removes one overlay element.' }
                ]),
                remove_by_classes: fn('Remove draw commands by classes', [
                    { params: [{ name: 'classes', type: 'string[]' }], returns: 'void', documentation: 'Removes all matching overlay elements.' }
                ]),
                clear: fn('Clear all overlay commands', [
                    { params: [], returns: 'void', documentation: 'Clears all active canvas elements.' }
                ]),
                set_display: fn('Attach overlay to a display', [
                    { params: [{ name: 'display_id', type: 'integer' }], returns: 'void', documentation: 'Moves overlay drawing target to display id.' }
                ])
            }),
            ocr: mod('Optical character recognition', {
                fast: mod('Fast OCR mode', {
                    recognize_text: fn('Recognize text', [
                        { params: [], returns: 'OCRVector', documentation: 'OCR over current screen frame.' },
                        { params: [{ name: 'options', type: '{region?:{x,y,w,h}}' }], returns: 'OCRVector', documentation: 'OCR on region of current frame.' },
                        { params: [{ name: 'buffer', type: 'PixelBuffer|PixelBufferReadOnly' }, { name: 'options', type: '{region?:{x,y,w,h}}' }], returns: 'OCRVector', documentation: 'OCR on explicit buffer.' }
                    ])
                }),
                accurate: mod('Accurate OCR mode', {
                    recognize_text: fn('Recognize text', [
                        { params: [], returns: 'OCRVector', documentation: 'OCR over current screen frame.' },
                        { params: [{ name: 'options', type: '{region?:{x,y,w,h}}' }], returns: 'OCRVector', documentation: 'OCR on region of current frame.' },
                        { params: [{ name: 'buffer', type: 'PixelBuffer|PixelBufferReadOnly' }, { name: 'options', type: '{region?:{x,y,w,h}}' }], returns: 'OCRVector', documentation: 'OCR on explicit buffer.' }
                    ])
                })
            })
        }),
        mouse: mod('Mouse input control', {
            Button: constants('Mouse button constants', {
                LEFT: { value: -3, description: 'Primary mouse button.' },
                RIGHT: { value: -2, description: 'Secondary mouse button.' },
                MIDDLE: { value: -1, description: 'Middle mouse button.' }
            }),
            EventType: constants('Mouse event constants', {
                DOWN: { value: -2, description: 'Mouse button down event.' },
                UP: { value: -1, description: 'Mouse button up event.' }
            }),
            move: fn('Move mouse cursor', [
                { params: [{ name: 'x', type: 'number' }, { name: 'y', type: 'number' }], returns: 'void', documentation: 'Move cursor instantly to absolute coordinates.' },
                { params: [{ name: 'x', type: 'number' }, { name: 'y', type: 'number' }, { name: 'duration', type: 'number' }], returns: 'void', documentation: 'Move cursor with duration interpolation.' }
            ]),
            click: fn('Click mouse button', [
                { params: [{ name: 'button', type: 'system.mouse.Button' }], returns: 'void', documentation: 'Single click at current cursor.' },
                { params: [{ name: 'button', type: 'system.mouse.Button' }, { name: 'clicks', type: 'integer' }], returns: 'void', documentation: 'Multiple clicks at current cursor.' },
                { params: [{ name: 'button', type: 'system.mouse.Button' }, { name: 'x', type: 'number' }, { name: 'y', type: 'number' }], returns: 'void', documentation: 'Single click at coordinates.' },
                { params: [{ name: 'button', type: 'system.mouse.Button' }, { name: 'x', type: 'number' }, { name: 'y', type: 'number' }, { name: 'clicks', type: 'integer' }], returns: 'void', documentation: 'Multiple clicks at coordinates.' }
            ]),
            send: fn('Send low-level mouse event', [
                { params: [{ name: 'button', type: 'system.mouse.Button' }, { name: 'event_type', type: 'system.mouse.EventType' }], returns: 'void', documentation: 'Send event at current cursor.' },
                { params: [{ name: 'button', type: 'system.mouse.Button' }, { name: 'event_type', type: 'system.mouse.EventType' }, { name: 'x', type: 'number' }, { name: 'y', type: 'number' }], returns: 'void', documentation: 'Send event at explicit coordinates.' }
            ]),
            get_position: fn('Get current mouse position', [
                { params: [], returns: 'number, number', documentation: 'Returns x and y coordinates.' }
            ])
        }),
        keyboard: mod('Keyboard input control', {
            EventType: constants('Keyboard event constants', {
                DOWN: { value: -2, description: 'Key down event.' },
                UP: { value: -1, description: 'Key up event.' }
            }),
            TypeMode: constants('Keyboard typing modes', {
                SEQUENTIAL: { value: -2, description: 'Type with per-character event sequence.' },
                STRING: { value: -1, description: 'Type as fast string input path.' }
            }),
            tap: fn('Alias for keyboard.press', [
                { params: [{ name: 'key', type: 'string' }], returns: 'void', documentation: "Presses and releases one key (e.g. 'a', 'enter')." }
            ]),
            press: fn('Press and release one key', [
                { params: [{ name: 'key', type: 'string' }], returns: 'void', documentation: "Presses and releases one key (e.g. 'a', 'enter')." }
            ]),
            send: fn('Send low-level key event', [
                { params: [{ name: 'event_type', type: 'system.keyboard.EventType' }, { name: 'key', type: 'string' }], returns: 'void', documentation: "Sends event for one key token (e.g. send(EventType.DOWN, 'shift'))." }
            ]),
            type: fn('Type text payload', [
                { params: [{ name: 'mode', type: 'system.keyboard.TypeMode' }, { name: 'text', type: 'string' }], returns: 'void', documentation: 'Types text in chosen mode.' },
                { params: [{ name: 'mode', type: 'system.keyboard.TypeMode' }, { name: 'text', type: 'string' }, { name: 'interval_ms', type: 'integer' }], returns: 'void', documentation: 'For SEQUENTIAL mode, controls per-key interval.' }
            ])
        }),
        opencv: mod('OpenCV helpers', {
            load_from_file: fn('Load image file as Mat', [
                { params: [{ name: 'filename', type: 'string' }], returns: 'Mat', documentation: 'Loads image into OpenCV Mat wrapper.' }
            ]),
            load_image: fn('Alias of load_from_file', [
                { params: [{ name: 'filename', type: 'string' }], returns: 'Mat', documentation: 'Alias for system.opencv.load_from_file.' }
            ])
        }),
        fs: mod('Manifest-aware file APIs', {
            open: fn('Open file handle', [
                { params: [{ name: 'path', type: 'string' }], returns: 'FileHandle', documentation: "Open file in default 'r' mode." },
                { params: [{ name: 'path', type: 'string' }, { name: 'mode', type: 'string' }], returns: 'FileHandle', documentation: "Open file with mode (r/w/a/r+/w+/a+, optional 'b')." }
            ]),
            read_all: fn('Read whole file contents', [
                { params: [{ name: 'path', type: 'string' }], returns: 'string', documentation: 'Reads complete file content.' }
            ]),
            write_all: fn('Write entire file', [
                { params: [{ name: 'path', type: 'string' }, { name: 'data', type: 'string' }], returns: 'boolean', documentation: 'Creates/truncates and writes content.' }
            ]),
            append: fn('Append file content', [
                { params: [{ name: 'path', type: 'string' }, { name: 'data', type: 'string' }], returns: 'boolean', documentation: 'Appends data to end of file.' }
            ]),
            exists: fn('Check if path exists', [
                { params: [{ name: 'path', type: 'string' }], returns: 'boolean', documentation: 'Returns true if file/directory exists.' }
            ]),
            stat: fn('Read file metadata', [
                { params: [{ name: 'path', type: 'string' }], returns: '{path:string,exists:boolean,is_file:boolean,is_dir:boolean,size:number}', documentation: 'Returns file stat object.' }
            ]),
            list: fn('List directory entries under manifest constraints', [
                { params: [], returns: 'string[]', documentation: 'Lists project root entries.' },
                { params: [{ name: 'path', type: 'string' }], returns: 'string[]', documentation: 'Lists entries under a path.' }
            ]),
            remove: fn('Remove file or directory', [
                { params: [{ name: 'path', type: 'string' }], returns: 'boolean', documentation: 'Deletes file/directory tree if permitted.' }
            ]),
            mkdir: fn('Create directory', [
                { params: [{ name: 'path', type: 'string' }], returns: 'boolean', documentation: 'Create directory with default recursive behavior.' },
                { params: [{ name: 'path', type: 'string' }, { name: 'recursive', type: 'boolean' }], returns: 'boolean', documentation: 'Create directory with explicit recursion mode.' }
            ]),
            rename: fn('Rename/move file', [
                { params: [{ name: 'src', type: 'string' }, { name: 'dst', type: 'string' }], returns: 'boolean', documentation: 'Moves src to dst path.' }
            ])
        })
    })
};

export const luaAPI = luaApiTree.system;

function getNodeForPath(parts) {
    let node = luaApiTree;
    for (const part of parts) {
        if (!part) continue;
        node = node?.[part];
        if (!node) return null;
    }
    return node;
}

function createFunctionSuggestions(memberName, def, range) {
    return def.overloads.map((overload, index) => {
        const params = overload.params.map((p, i) => `\${${i + 1}:${p.name}}`).join(', ');
        const signature = overload.params.map((p) => `${p.name}: ${p.type}`).join(', ');
        return {
            label: def.overloads.length > 1 ? `${memberName}(${signature})` : memberName,
            kind: monaco.languages.CompletionItemKind.Function,
            insertText: `${memberName}(${params})`,
            insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
            documentation: {
                value: `**${def.description}**\n\n${overload.documentation}\n\n*Returns:* \`${overload.returns}\``,
                isTrusted: true
            },
            detail: `(${signature}) -> ${overload.returns}`,
            sortText: `1_${memberName}_${index}`,
            commitCharacters: ['('],
            range
        };
    });
}

function createModuleSuggestion(memberName, moduleDef, range) {
    return {
        label: memberName,
        kind: monaco.languages.CompletionItemKind.Module,
        insertText: memberName,
        documentation: moduleDef._moduleDescription || `Module: ${memberName}`,
        sortText: `0_${memberName}`,
        commitCharacters: ['.'],
        range
    };
}

function createConstantSuggestions(consts, range) {
    return Object.entries(consts).map(([name, info]) => ({
        label: name,
        kind: monaco.languages.CompletionItemKind.EnumMember,
        insertText: name,
        documentation: `${info.description}\n\nValue: \`${info.value}\``,
        detail: String(info.value),
        sortText: `2_${name}`,
        range
    }));
}

function collectSuggestions(node, range) {
    if (!node || typeof node !== 'object') return [];
    if (node._constants) return createConstantSuggestions(node._constants, range);

    const out = [];
    for (const [memberName, memberValue] of Object.entries(node)) {
        if (memberName.startsWith('_')) continue;
        if (memberValue?.overloads) {
            out.push(...createFunctionSuggestions(memberName, memberValue, range));
        } else if (memberValue?._constants) {
            out.push(createModuleSuggestion(memberName, memberValue, range));
        } else if (typeof memberValue === 'object') {
            out.push(createModuleSuggestion(memberName, memberValue, range));
        }
    }
    return out;
}

function resolvePathFromText(text) {
    const match = text.match(/([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)$/);
    if (!match) return null;
    return match[1].split('.');
}

function resolveRange(position, token = '') {
    return {
        startLineNumber: position.lineNumber,
        endLineNumber: position.lineNumber,
        startColumn: position.column - token.length,
        endColumn: position.column
    };
}

function getCallableAtPath(pathParts, fnName) {
    const node = getNodeForPath(pathParts);
    if (!node || typeof node !== 'object') return null;
    const fnDef = node[fnName];
    return fnDef?.overloads ? fnDef : null;
}

export function registerLuaAPI(monacoInstance) {
    monaco = monacoInstance;
    
    monaco.languages.registerCompletionItemProvider('lua', {
        triggerCharacters: ['.'],
        provideCompletionItems: (model, position) => {
            const textUntilPosition = model.getValueInRange({
                startLineNumber: 1,
                startColumn: 1,
                endLineNumber: position.lineNumber,
                endColumn: position.column
            });
            
            const pathParts = resolvePathFromText(textUntilPosition);
            if (!pathParts || pathParts.length === 0) return { suggestions: [] };

            const partial = pathParts[pathParts.length - 1] || '';
            const isAfterDot = textUntilPosition.endsWith('.');
            const basePath = isAfterDot ? pathParts : pathParts.slice(0, -1);
            const replaceToken = isAfterDot ? '' : partial;
            const range = resolveRange(position, replaceToken);

            const node = getNodeForPath(basePath);
            if (!node) return { suggestions: [] };

            const suggestions = collectSuggestions(node, range);
                return { suggestions };
        }
    });
    
    monaco.languages.registerSignatureHelpProvider('lua', {
        signatureHelpTriggerCharacters: ['(', ','],
        provideSignatureHelp: (model, position) => {
            const textUntilPosition = model.getValueInRange({
                startLineNumber: 1,
                startColumn: 1,
                endLineNumber: position.lineNumber,
                endColumn: position.column
            });
            
            const callMatch = textUntilPosition.match(/([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\(([^\)]*)$/);
            if (!callMatch) return null;

            const fullPath = callMatch[1].split('.');
            const fnName = fullPath[fullPath.length - 1];
            const nodePath = fullPath.slice(0, -1);
            const fnDef = getCallableAtPath(nodePath, fnName);
            if (!fnDef) return null;

            const commaCount = (callMatch[2].match(/,/g) || []).length;
            const signatures = fnDef.overloads.map((overload) => ({
                label: `${fnName}(${overload.params.map((p) => `${p.name}: ${p.type}`).join(', ')}) -> ${overload.returns}`,
                documentation: overload.documentation,
                parameters: overload.params.map((p) => ({ label: `${p.name}: ${p.type}`, documentation: p.type }))
            }));
            
            return {
                value: {
                signatures,
                activeSignature: 0,
                    activeParameter: Math.min(commaCount, Math.max(0, signatures[0].parameters.length - 1))
                },
                dispose: () => {}
            };
        }
    });
    
    monaco.languages.registerHoverProvider('lua', {
        provideHover: (model, position) => {
            const word = model.getWordAtPosition(position);
            if (!word) return null;
            
            const line = model.getLineContent(position.lineNumber);
            const textBefore = line.slice(0, word.endColumn - 1);
            const pathMatch = textBefore.match(/([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)$/);
            if (!pathMatch) return null;

            const parts = pathMatch[1].split('.');
            const last = parts[parts.length - 1];
            const parent = getNodeForPath(parts.slice(0, -1));
            if (!parent) return null;

            const node = parent[last];
            if (!node) return null;

            if (node.overloads) {
                const overloads = node.overloads
                    .map((o, i) => `**Overload ${i + 1}:** \`${last}(${o.params.map((p) => `${p.name}: ${p.type}`).join(', ')}) -> ${o.returns}\`\n\n${o.documentation}`)
                    .join('\n\n---\n\n');
                return { contents: [{ value: `**${pathMatch[1]}**\n\n${node.description}\n\n${overloads}` }] };
            }

            if (node._constants) {
                const values = Object.entries(node._constants)
                    .map(([k, v]) => `- \`${k}\` = \`${v.value}\` (${v.description})`)
                    .join('\n');
                return { contents: [{ value: `**${pathMatch[1]}**\n\n${node._constantsDescription || ''}\n\n${values}` }] };
            }

            if (node._moduleDescription) {
                return { contents: [{ value: `**${pathMatch[1]}**\n\n${node._moduleDescription}` }] };
            }
            
            return null;
        }
    });
}
