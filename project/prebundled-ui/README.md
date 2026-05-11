# Prebundled UI Contract

The user-facing offline build flow (`cmake/user/CMakeLists.txt`) expects UI files here.

## Required structure

- `project/prebundled-ui/main/main.html`
- `project/prebundled-ui/main/assets/...`
- `project/prebundled-ui/overlay/macro_controls.html`
- `project/prebundled-ui/overlay/assets/...`

## Licensing (for readers of this repo)

Machotkey-original UI shipped under this directory is covered by the repository **[LICENSE](../../LICENSE)** (source-available), like the rest of Machotkey-original code. Some files under **`assets/`** may come from **third-party** frontend dependencies; those remain under **their** authors’ licenses—see notices in the tree or in shipped materials. A fuller overview for **users** is in **[LEGAL.md](../../LEGAL.md)**.

## How the offline bundle is produced (build notes)

After a local UI build, copy from the app bundle output paths:

- `build/machotkey.app/Contents/Resources/main/**` -> `project/prebundled-ui/main/**`
- `build/machotkey.app/Contents/Resources/overlay/**` -> `project/prebundled-ui/overlay/**`

The copy should include the full `assets/` directories with hashed files.
