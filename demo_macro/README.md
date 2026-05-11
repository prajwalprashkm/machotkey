# Machotkey demo & benchmark (clean slate)

This project is a **readable demo** of Machotkey macro APIs, shaped like **`macro_project`** (early `config_ui.setup`, `regions.init`, then `begin_capture` last). It also **measures the capture stream** (FPS, mean frame latency, raw FPS including drops) under several workloads—**one phase at a time** or via an **automated suite**.

## What you see

- **Overlay canvas**: live HUD (phase, FPS, latency, dropped frames, workload ops / µs per op).
- **WebView panel**: phase buttons, suite controls, last metric window per phase, optional `config_user.lua` tuning.
- **Hotkeys**: **Ctrl+P** pause/resume capture callback work; **Ctrl+Q** quit.

## Phases (work done *inside* the capture callback)

| Phase | Load |
|--------|------|
| **idle** | Baseline: only capture + canvas updates. |
| **color** | Many full-frame `system.screen.find_color` calls per frame (RGB / tolerance sweep). |
| **ocr_fast** | **One** full-frame `recognize_text({})` per frame — timing reports that single OCR pass. |
| **ocr_accurate** | **One** `ocr.accurate` full-frame pass per frame (timing = that pass). |
| **input** | Batch `system.mouse.get_position` only (no synthetic mouse clicks). |
| **opencv** | **One** pipeline per frame: crops, `to_opencv_mat` ×2, **one** `match_template` — timing is that full task. |
| **fs** | Several `system.fs.read_all("config.lua")` per frame (manifest allowlist). |

Batched phases (**color**, **input**, **fs**) report **average µs per operation** across many calls. **OCR** and **OpenCV** run **one heavy task** per frame; `workload_ops` is **1** and **`workload_us_per_op`** equals that task’s duration in µs (same as **`workload_total_ms`** × 1000).

Metrics for the **current** phase are recomputed every **`METRICS_WINDOW_FRAMES`** frames and stored under `state.bench_results[phase]` (`fps`, `latency_ms`, `raw_fps`, plus the workload fields above).

## Full suite

**Run full suite** walks phases in order (`idle` → `color` → … → `fs`), holding each for **`SUITE_PHASE_MS`** (default 2.8s) so numbers can stabilize. **Stop suite** cancels the timer chain.

## Files (skim order)

| File | Role |
|------|------|
| `main.lua` | `config_ui` → `regions` → `capture.start` (macro-style ordering). |
| `capture.lua` | `begin_capture`, canvas HUD, metrics windows, calls `workloads`. |
| `workloads.lua` | Per-phase synthetic load (color / OCR / input / OpenCV / FS). |
| `suite.lua` | Timed phase advances + toasts. |
| `regions.lua` | Screen/layout ROIs (`init(config, geometry)`). |
| `geometry.lua` | Shared rect helpers (`round_rect`, …). |
| `config.lua` / `config_user.lua` | Defaults and overrides. |
| `config_ui.lua` | WebView: `ui.open`, `macroUI` events, `fs.open` saves. |
| `state.lua` | Phase, suite flag, rolling metrics, `bench_results`. |
| `ui/*` | Panel markup + `emit("ui_ready")` pattern like `macro_project`. |

## Safety note

The **input** phase only stresses **mouse position queries**, not random clicks. **OCR** and **OpenCV** scale sharply with capture resolution; **`OPENCV_TEMPLATE_SIZE`** is still tunable in the panel / `config_user.lua`.

## License

Lua and assets in **`demo_macro/`** are part of the Machotkey repository and are covered by the same **[source-available terms](../LICENSE)** as the rest of the project’s original work, unless a file says otherwise. Vendored deps used **by the host app** when you run this macro live under **`project/libs/`**, **`project/include/httplib/`**, and **`project/include/nlohmann/`** licenses. See **[LEGAL.md](../LEGAL.md)** for the full picture.
