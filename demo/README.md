# macro_project_demo

A generic demo macro project intended to showcase core Machotkey runtime features in a single, easy-to-read package.

## What this demo uses

- `system.on_key(...)` hotkeys:
  - `Ctrl+P`: pause/resume
  - `Ctrl+A`: arm/toggle automation
  - `Ctrl+S`: flag snapshot marker
  - `Ctrl+Q`: stop capture and exit
- `system.screen.begin_capture(...)` with FPS and region options
- `system.screen.find_color(...)` on a configurable scan region
- `system.screen.canvas.*` overlay rendering (`rect`, `text`, `clear`)
- timestamp + frame index telemetry:
  - `system.screen.get_current_timestamp()`
  - `system.screen.timestamp`
  - `system.screen.index`
- process stats via `system.stats.get_info("mb")`
- delayed operations via `system.set_timeout(...)`
- background task loop via `system._create_task(...)`
- mouse + keyboard automation:
  - `system.mouse.send(...)`
  - `system.keyboard.press(...)`
- embedded UI + event bridge:
  - `system.ui.open(...)`
  - `system.ui.on(...)`
  - `win:run_js(...)`
- file persistence through sandbox FS API:
  - `system.fs.open(...)` writing `config_user.lua`

## Files

- `main.lua` entry, hotkeys, module startup
- `capture.lua` capture loop + on-screen telemetry + color scan
- `automation.lua` background automation task
- `ui_controller.lua` bridge between Lua runtime and UI controls/events
- `config.lua` defaults + optional overrides from `config_user.lua`
- `ui/*` simple control panel (save config, toggles, runtime status)

## Running

Use this folder as a macro project with `manifest.json` + `main.lua` entry.
