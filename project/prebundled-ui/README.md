# Prebundled UI Contract

The user-facing offline build flow (`cmake/user/CMakeLists.txt`) expects UI files here.

## Required structure

- `project/prebundled-ui/main/main.html`
- `project/prebundled-ui/main/assets/...`
- `project/prebundled-ui/overlay/macro_controls.html`
- `project/prebundled-ui/overlay/assets/...`

## How to refresh from maintainer Vite output

Copy from your existing Vite output paths:

- `build/machotkey.app/Contents/Resources/main/**` -> `project/prebundled-ui/main/**`
- `build/machotkey.app/Contents/Resources/overlay/**` -> `project/prebundled-ui/overlay/**`

The copy should include the full `assets/` directories with hashed files.
