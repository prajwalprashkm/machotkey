# User Build Guide (Offline, arm64 macOS 14+)

This guide describes the user-facing build flow available directly from root `CMakeLists.txt`.

## Audience

- End users building natively from source.
- Offline-first build behavior.
- No Homebrew dependency path required by default.

## Requirements

- Apple Silicon Mac (arm64)
- macOS 14+
- Xcode Command Line Tools (or full Xcode toolchain)
- Git with submodule support
- Python 3

Node/npm is not required in this flow.

## Dependency resolution (offline, submodule-first)

- Bundled source tree is the default dependency source.
- No Homebrew dependency path is required in this flow.
- No build-time network fetching is performed.

### Dependencies handled automatically in user flow

- **OpenCV**: built from bundled source under `project/libs/opencv`.
- **LuaJIT**: built from bundled source under `project/libs/luajit/src`.
- **Eigen**: resolved from bundled source include path (`project/libs/eigen`).
- **ImGui**: compiled directly from bundled source in `project/libs/imgui`.
- **Header-only vendored libs**: `sol2`, `httplib`, and `nlohmann/json` are consumed directly as source includes.

### Removed manual "custom built by me" requirement

In this user flow, end users no longer need to manually prebuild custom local install trees for:

- Eigen
- OpenCV
- LuaJIT

## Clone

Clone with submodules so local dependency source is present:

```bash
git clone --recurse-submodules <your-repo-url> machotkey-release
cd machotkey-release
```

If you already cloned:

```bash
git submodule update --init --recursive
```

## Prebundled UI Contract

Default build mode expects:

- `project/prebundled-ui/main/main.html`
- `project/prebundled-ui/main/assets/...`
- `project/prebundled-ui/overlay/macro_controls.html`
- `project/prebundled-ui/overlay/assets/...`

These will be copied into:

- `${CMAKE_BINARY_DIR}/machotkey.app/Contents/Resources/main/`
- `${CMAKE_BINARY_DIR}/machotkey.app/Contents/Resources/overlay/`

## Configure + Build

```bash
cmake -S . -B build-user
cmake --build build-user -j
```

## Output

App bundle output:

- `build-user/machotkey.app`

The bundle contains both executables:

- `build-user/machotkey.app/Contents/MacOS/machotkey`
- `build-user/machotkey.app/Contents/MacOS/macro_runner`

## Scope

This entrypoint is intentionally fixed to:

- Offline dependency resolution
- Prebundled UI copy into app resources
- Unsigned local user builds
