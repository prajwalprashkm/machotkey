# Licensing (for users and readers)

This page explains how rights are split in this repository. **Binding terms** for Machotkey-original work are in [LICENSE](LICENSE); this file is a **plain-language guide** and is **not legal advice**.

## Who owns Machotkey-original work

**Machotkey-original** work includes application sources, build/tooling, `demo_macro/`, the docs site under `docs/`, prebundled UI under `project/prebundled-ui/` (the project’s shipped HTML/CSS/JS), and everything **except** the [vendored paths](#vendored-third-party-code) listed below.

That original work is owned by **the copyright holder identified in the copyright notice at the beginning of [LICENSE](LICENSE)**. It is offered only under the terms of that file (source-available; **not** OSI “open source”).

## What you are allowed to do (Machotkey-original)

Read [LICENSE](LICENSE) for the exact wording. In short, **you** (any reader or user) are generally allowed to:

- **Inspect** the source: clone, read, and keep a private copy to understand, verify, or audit the software.
- For **personal, non‑commercial** evaluation, **build and run locally** on your own machines as described in `LICENSE`.

**You are not** generally allowed—without **written permission from the copyright holder**—to redistribute source or substantial excerpts, ship derivative or competing products based on this code, strip notices, or distribute binaries you built, except as `LICENSE` explicitly allows (e.g. sharing links to the official repository).

**You must** keep notices intact: If you build, use, or audit this code, you must not remove or modify the copyright notices or the LICENSE file. This applies to the notices found in the root directory and the headers of individual source files.

## Vendored third-party code

These trees are **not** Machotkey-original. They stay under **their** licenses and copyrights:

| Path | Typical upstream / license |
|------|----------------------------|
| **`project/libs/`** | OpenCV, LuaJIT, Eigen, FFmpeg, ImGui, sol2, … (see each subtree; Apache-2.0, MIT, LGPL, MPL-2.0, …) |
| **`project/include/httplib/`** | [cpp-httplib](https://github.com/yhirose/cpp-httplib) (**MIT**, header) |
| **`project/include/nlohmann/`** | [nlohmann/json](https://github.com/nlohmann/json) (**MIT**, SPDX in headers) |

**You must not** remove or falsify copyright or license notices in those vendored files.

If **you** distribute a combined work (for example an app that links these libraries), **you** must satisfy **those** licenses too (attribution, LGPL relinking rules where applicable, etc.), in addition to the rules in [LICENSE](LICENSE) for Machotkey-original parts.

## Prebundled web UI

The UI under `project/prebundled-ui/` is **Machotkey-original** work under [LICENSE](LICENSE), except that **`assets/`** may contain **third-party** scripts, fonts, or themes from the frontend build. Those embedded files follow **their** upstream licenses—check notices shipped with the project or in the repository.

## Apple and other systems

Use of Apple **system frameworks** (AppKit, WebKit, ScreenCaptureKit, …) is governed by **Apple’s** terms, separate from Machotkey’s `LICENSE`.

## Third-party contributions

If the project ever **accepts contributions from others**, the copyright holder may use a contributor agreement or other written terms so the project can keep enforcing [LICENSE](LICENSE). That does not change **your** rights as a user today; read [LICENSE](LICENSE) and this page for what applies to **you**.
