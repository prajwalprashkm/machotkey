# Machotkey demo & benchmark (clean slate)

This project is a **readable demo** of Machotkey macro APIs, shaped like **`macro_project`** (early `config_ui.setup`, `regions.init`, then `begin_capture` last). It also **measures the capture stream** (FPS, mean frame latency, raw FPS including drops) under several workloads—**one phase at a time** or via an **automated suite**.

## What you see

- **Overlay canvas**: live HUD (phase, **capture callbacks/s**, latency, **primary action ops/s** from summed workload time, window-averaged µs/op, last batch line, dropped frames, runner stats).
- **WebView panel**: phase buttons, suite controls, last metric window per phase, optional `config_user.lua` tuning, **runner process** CPU % and RAM / virtual size (MiB) from `system.stats.get_info`.
- **Hotkeys**: **Ctrl+P** pause/resume capture callback work; **Ctrl+Q** quit.

## Phases (work done *inside* the capture callback)

| Phase | Load |
|--------|------|
| **idle** | Baseline: only capture + canvas updates. |
| **color** | Many full-frame `system.screen.find_color` calls per frame (RGB / tolerance sweep). |
| **ocr_fast** | **One** full-frame `recognize_text({})` per frame — timing reports that single OCR pass. |
| **ocr_accurate** | **One** `ocr.accurate` full-frame pass per frame (timing = that pass). |
| **input** | Batch `system.mouse.get_position` only (no synthetic mouse clicks). |
| **opencv** | **One** pipeline per frame after you confirm the **OpenCV CPU** dialog in the panel (**OK**). Until then the phase is selected but no OpenCV work runs; the **suite** also waits on **OK** before its timer advances. |
| **fs** | Several `system.fs.read_all("config.lua")` per frame (manifest allowlist). |

Batched phases (**color**, **input**, **fs**) time a whole batch per callback; **`action_ops_per_sec`** is **`sum(ops) ÷ sum(workload µs)`** over the window (throughput of the timed work). **OCR** and **OpenCV** use **one** timed op per callback. The HUD **last batch** line is the most recent callback only; **`action_avg_us_per_op`** is **`sum(workload µs) ÷ sum(ops)`** over the full window.

Metrics for the **current** phase are recomputed every **`METRICS_WINDOW_FRAMES`** capture callbacks and stored under `state.bench_results[phase]` (one row of keys per phase, no duplicate field names).

- **`callbacks_per_sec`**: capture callbacks completed per **wall‑clock** second for the window (first callback start → last workload finish).
- **`callback_raw_fps`**: same window, but the callback count **includes** skipped frame indices (stream pressure / drops).

**Action throughput** (timed workload only):

- **`action_ops_per_sec`**: `sum(ops) ÷ sum(timed workload µs)` over the window—ops per second for the measured batches, not scaled by wall time between callbacks.
- **`action_avg_us_per_op`**: `sum(workload µs) ÷ sum(ops)` over the window.
- **`action_label`**: which API the rate refers to (`find_color`, `mouse.get_position`, etc.).

**Last callback in the window** (HUD “last batch”): **`workload_ops_last_batch`**, **`workload_us_per_op_last_batch`**, **`workload_total_ms_last_batch`**.

Live `state.fps` / `state.raw_fps` in the HUD are the same numbers as **`callbacks_per_sec`** / **`callback_raw_fps`**; only **`bench_results`** JSON uses the explicit names.

The WebView **What each JSON field means** section matches these keys.

## Full suite

**Run full suite** walks phases in order (`idle` → `color` → … → `fs`). For each phase it **waits until one full metrics window** (`METRICS_WINDOW_FRAMES` callbacks) has been recorded so **`bench_results`** and the WebView summary always get a row for slow phases (color, OCR). Then it dwells **`SUITE_PHASE_MS`** before the next phase. For **opencv**, the chain continues only after you press **OK** on the CPU dialog, then the same metrics wait + dwell apply. If metrics never complete (e.g. paused), a **fallback timeout** (~4× `SUITE_PHASE_MS`, minimum 45s) advances anyway. **Stop suite** cancels waiters and bumps an internal generation so stale timers do nothing.

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
