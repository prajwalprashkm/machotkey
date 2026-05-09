# Machotkey

Machotkey is a very high performance native macOS macro scripting application: write macros in **Lua** (LuaJIT), automate keyboard and mouse, capture and analyze the screen with **OpenCV**, and drive the UI through embedded **WebKit** views with an optional **ImGui** overlay path.

The design splits privileges cleanly:

- **machotkey** — main host process (UI, elevated capabilities).
- **macro_runner** — sandboxed child that runs Lua scripts and talks to the host over IPC and shared memory.

That separation keeps everyday scripting constrained while the host can perform platform actions on behalf of trusted workflows.

## Customization

Machotkey is built to be **tunable end-to-end**:

- **Scripts** — macros are plain **Lua** on LuaJIT; you shape logic, timing, and structure without recompiling the app.
- **Rate limits** — **Configurable throttling** on hot paths so you can trade throughput vs CPU usage for capture, automation, and polling loops.
- **Permissions model** — capabilities are driven by a **manifest-style permission audit** (declared intents vs what the runtime allows), with workflows for **secure permission reset** when you change scope.
- **Trust on edit** — when macro **source changes**, the tooling can **re-verify hashes** so altered scripts do not silently keep stale privileges (reduces the chance of tampered code running with broader access than you approved).
- **UI** — Web-based panels plus optional overlay paths let you adapt editor and control surfaces without rewriting the native core.

Exact knobs live in project settings, sandbox profiles, and Lua APIs as documented in-repo.

## Performance and safety (measured highlights)

Figures below are **author-measured** on capable Apple Silicon setups (resolution, refresh rate, display count, and macro workload affect real numbers). Treat them as indicative of what the architecture targets, not a guarantee on every machine. Tested on a Macbook Air M2 chip with 8 GB of unified memory.

| Area | Result |
|------|--------|
| **Frame pipeline** | Sub-**0.1 ms** typical **delivery latency** from capture path into consumers when tuned for throughput |
| **Capture throughput** | **200+ FPS** frame capture in favorable configurations; **ceilings track monitor refresh** and system load |
| **Color search** | On the order of **~2 ms** for a **full high-DPI frame** scan using the optimized search path (content-dependent) |
| **Input automation** | **Low-latency** synthesized keyboard and mouse events intended for interactive automation |
| **Defense in depth** | **Seatbelt-style sandboxing** for the macro runner to contain malicious or buggy scripts; host stays privileged but strictly mediates dangerous operations |

Together with customizable limits and permission auditing, the stack is aimed at **interactive automation and vision-heavy macros** where latency and throughput matter.

## Specs and requirements

| Item | Detail |
|------|--------|
| **Platform** | macOS **14.0+** (see `LSMinimumSystemVersion` in `project/resources/Info.plist`) |
| **CPU** | **Apple Silicon (arm64)** — CMake targets arm64. Usage depends on the macros run. |
| **RAM** | No hard minimum enforced; the app itself can run with under 200 MB. However usage depends on the macros run. |
| **Toolchain** | Xcode Command Line Tools or full Xcode (Clang, SDK) |
| **Git** | Submodules required for bundled dependencies |
| **Python 3** | Used at build time to generate embedded resources (`embed.h`) |
| **Node.js** | **Not** required for the default open-source build (prebundled UI under `project/prebundled-ui/`) |

Third-party libraries used by the default build are vendored under `project/libs/` (OpenCV, LuaJIT, Eigen, ImGui, sol2, etc.), typically tracked as **git submodules**. Clone with `--recurse-submodules`.

Machotkey can run well even on low specs, and consumes very little RAM overhead (usually under 200 MB for the application itself and near zero for the runner). CPU usage overhead is also minimal.

## Tested configuration

End-to-end builds have been exercised on:

- **Apple M2 MacBook Air**, **8 GB RAM**, macOS with Apple Clang / Xcode toolchain.

Other Apple Silicon Macs with sufficient disk and RAM should work; first-time OpenCV builds are CPU- and memory-intensive.

## Building from source

From the repository root:

```bash
git clone --recurse-submodules https://github.com/prajwalprashkm/machotkey.git
cd machotkey

mkdir build && cd build
cmake ..
cmake --build . -j
```

Or an out-of-tree build directory of your choice:

```bash
cmake -S . -B build
cmake --build build -j
```

Outputs:

- `build/machotkey.app` (working app bundle)

Release-style optimizations are the default for single-configuration generators (`CMAKE_BUILD_TYPE` defaults to **Release**). Unsigned local builds are expected unless you add your own signing step.

### Maintainer workflow

- **`CMakeLists.local.txt`** — optional local maintainer CMake entrypoint (not required for the default root build).
- **Prebundled UI** — `project/prebundled-ui/main/` and `project/prebundled-ui/overlay/` must contain the shipped HTML and hashed assets (see `project/prebundled-ui/README.md`).

Dependency knobs live under **`cmake/user/`** (included from root `CMakeLists.txt`).

## License

Add your license here when you publish (for example MIT, Apache-2.0, or GPLv3).
