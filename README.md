# Machotkey

Native macOS macro scripting application: write macros in **Lua** (LuaJIT), automate keyboard and mouse, capture and analyze the screen with **OpenCV**, and drive the UI through embedded **WebKit** views with an optional **ImGui** overlay path.

The design splits privileges cleanly:

- **machotkey** — main host process (UI, elevated capabilities).
- **macro_runner** — sandboxed child that runs Lua scripts and talks to the host over IPC and shared memory.

That separation keeps everyday scripting constrained while the host can perform platform actions on behalf of trusted workflows.

## Specs and requirements

| Item | Detail |
|------|--------|
| **Platform** | macOS **14.0+** (see `LSMinimumSystemVersion` in `project/resources/Info.plist`) |
| **CPU** | **Apple Silicon (arm64)** — CMake targets arm64 |
| **RAM** | No hard minimum enforced; **8 GB** is workable for development builds (OpenCV from source is the heaviest step). More RAM speeds clean rebuilds. |
| **Toolchain** | Xcode Command Line Tools or full Xcode (Clang, SDK) |
| **Git** | Submodules required for bundled dependencies |
| **Python 3** | Used at build time to generate embedded resources (`embed.h`) |
| **Node.js** | **Not** required for the default open-source build (prebundled UI under `project/prebundled-ui/`) |

Third-party libraries used by the default build are vendored under `project/libs/` (OpenCV, LuaJIT, Eigen, ImGui, sol2, etc.), typically tracked as **git submodules**. Clone with `--recurse-submodules`.

## Tested configuration

End-to-end builds have been exercised on:

- **Apple M2 MacBook Air**, **8 GB RAM**, macOS with Apple Clang / Xcode toolchain.

Other Apple Silicon Macs with sufficient disk and RAM should work; first-time OpenCV builds are CPU- and memory-intensive.

## Building from source

From the repository root:

```bash
git clone --recurse-submodules https://github.com/<your-org>/machotkey.git
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

- `build/machotkey.app` (or `build-user/…` if you used another folder name)
- Inside the bundle: **`machotkey`** and **`macro_runner`** under `Contents/MacOS/`

Release-style optimizations are the default for single-configuration generators (`CMAKE_BUILD_TYPE` defaults to **Release**). Unsigned local builds are expected unless you add your own signing step.

### Maintainer workflow

- **`CMakeLists.local.txt`** — optional local maintainer CMake entrypoint (not required for the default root build).
- **Prebundled UI** — `project/prebundled-ui/main/` and `project/prebundled-ui/overlay/` must contain the shipped HTML and hashed assets (see `project/prebundled-ui/README.md`).

Dependency knobs live under **`cmake/user/`** (included from root `CMakeLists.txt`).

## License

Add your license here when you publish (for example MIT, Apache-2.0, or GPLv3).
