# Real Overlay Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace this morning's hand-authored 5×7 pixel-art overlay icons — plus the quickmenu's bitmap-font title and hint line — with real vector-sourced art (battery/WiFi from Batocera/ES's own icons, volume/brightness/title/button-badges newly hand-drawn to match), baked to fixed pixel bitmaps at build-tool time and alpha-blended onto the framebuffer at draw time. Also moves the quickmenu's icon and percentage bar onto one row instead of two.

**Architecture:** A one-time, developer-run Python script (`tools/convert-icons.py`, using CairoSVG) rasterizes 14 source SVGs (9 status icons + 1 title wordmark + 4 button badges) to fixed-size alpha-only bitmaps, emitting a generated-but-committed `qm_icon_data.c`. `qm_icons.c` is rewritten around a single `qm_draw_icon_rgba()` alpha-blend blit (no runtime scaling — every asset is baked to its exact final on-screen pixel size, and icons render at that fixed size on both panel resolutions) plus a pure `qm_battery_icon_kind()` mapper. `quickmenu.c` and `sb_render.c` switch their draw calls to the new API; `quickmenu.c` additionally moves its icon+bar layout onto one row and replaces its title/hint-line text with the new title and button-badge assets.

**Tech Stack:** C (gnu99, no toolkit, same as the rest of both overlay packages), Python 3 + CairoSVG + Pillow (host-only, developer-run, never touches the Buildroot build), host `cc` for tests (`tests/run-c-tests.sh`), Buildroot `generic-package`.

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-real-overlay-icons-design.md`, including its "Addendum: title wordmark + button badges" section — this plan implements both exactly; do not deviate from the architecture (one-time developer-run conversion, alpha-only baked data at a fixed final on-screen size, no runtime SVG/image decode library, no runtime icon scaling, icon+bar same-row layout, single-color badges via SVG `<mask>` cutout) or scope.
- **This plan REPLACES already-committed pixel-art icon work from earlier today** (commits `a843419975`, `ca13101cb3` in the batocera.linux repo — `qm_icons.c`, the 6-member `qm_icon_kind` enum, `qm_draw_icon()`/`qm_icon_width()`/`qm_icon_level_from_percent()`, and their call sites in `quickmenu.c`/`sb_render.c`). Every task must read the CURRENT state of a file fresh before editing it — do not assume any prose in this plan or the design doc is the literal current code; verify against the real file first.
- **Sizing**: the 9 status icons and 4 button badges all bake to `QM_ICON_SIZE = 24` (24×24px, square). The title wordmark bakes to its own size, `QM_TITLE_W = 200`, `QM_TITLE_H = 28` (wide, not square). Every baked asset renders at ITS OWN fixed pixel size regardless of which panel (640px or 320px) is in use — icons do **not** get multiplied by the existing `scale` factor the way text and bars still do. This is a deliberate, plan-level correction: an earlier draft of this plan multiplied icon size by `scale`, which contradicts the design's own "baked to final on-screen size, no runtime scaling" principle and would have made icons a different apparent size on each panel. `qm_icon_asset` (declared in `quickmenu.h`) carries `width`/`height` per asset so `qm_draw_icon_rgba` never assumes a single fixed size.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`). Every task's file changes happen here and get real git commits in this repo.
- **`batocera-build/scripts/*.sh` (`build-image.sh`, `env.sh`) default `BATOCERA_SRC` to a stale, unpatched checkout** at `batocera-build/build/batocera.linux` inside the main project directory — NOT where this project's development happens. Any command that sources `env.sh` or invokes `make` in the build tree MUST explicitly `export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux` first. `env.sh` itself requires `bash`, not `zsh` — wrap in `bash -c '...'` if the shell is zsh. Do not use `batocera-build/scripts/build-image.sh` for this plan's verification steps — it triggers a multi-hour full image build, which this plan does not need.
- Per Hard Rule #7 in the project's root `CLAUDE.md` (Buildroot incremental-build staleness gotcha): `circuitsword-quickmenu` and `circuitsword-statusbar` are both real compiled packages — use `PKG=circuitsword-quickmenu-rebuild` / `PKG=circuitsword-statusbar-rebuild` (not `-reinstall`) for verification.
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- `tools/convert-icons.py` requires `cairosvg` and `pillow` (`pip install cairosvg pillow`) — a one-time developer-machine install, not a Buildroot dependency of any kind. It is never invoked by any `.mk` file. It depends on whatever sans-serif bold font is installed on the machine that runs it (used by the title and badge SVGs' `<text>` elements) — a one-time, developer-machine concern, not something that affects the shipped binary or runs on-device.
- All overlay text stays English (title, hint-line labels "SELECT"/"BACK"/"ADJUST") — this was already the existing convention before this plan; nothing here changes copy or language, only how the title and button names render.
- After all build-tree changes are committed (end of Task 3), regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- No hardware in CI. Testing is off-device only: host `cc`-compiled C tests (`tests/run-c-tests.sh`) and incremental Buildroot single-package compiles (`bcm2837-pkg PKG=<name>-rebuild`). On-device validation (icon/title/badge legibility, layout proportions at both panel scales) is tracked in the findings log, never claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-REAL-ICONS-FINDINGS.md` (created in Task 3).

---

## Task 1: Source SVGs (14 total) + `tools/convert-icons.py` + generated `qm_icon_data.c`

**Files:**
- Create: 9 status-icon SVGs under `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/icons/src/` — `battery-empty.svg`, `battery-25.svg`, `battery-50.svg`, `battery-75.svg`, `battery-full.svg`, `battery-charging.svg`, `wifi.svg`, `volume.svg`, `brightness.svg`
- Create: `.../icons/src/title.svg`, `.../icons/src/badge-a.svg`, `.../icons/src/badge-b.svg`, `.../icons/src/badge-left.svg`, `.../icons/src/badge-right.svg`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/tools/convert-icons.py`
- Create (generated by the script, committed): `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c`

**Interfaces:**
- Produces: `QM_ICON_SIZE` (24), `QM_TITLE_W`/`QM_TITLE_H` (200/28), a 14-member `qm_icon_kind` enum + `QM_ICON_COUNT`, `qm_icon_asset { int width; int height; const uint8_t *alpha; }`, `extern const qm_icon_asset qm_icon_assets[QM_ICON_COUNT]` — all in `quickmenu.h`. `qm_icon_data.c` defines the array.

### Step 1: Copy the 9 status-icon source SVGs verbatim

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-empty.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="battery0.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-25.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="battery25.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 2 6 L 2 10 L 4 10 L 4 6 L 2 6 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-50.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="battery50.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 2 6 L 2 10 L 7 10 L 7 6 L 2 6 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-75.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="battery75.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 2 6 L 2 10 L 9 10 L 9 6 L 2 6 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-full.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="battery100.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 2 6 L 2 10 L 12 10 L 12 6 L 2 6 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/battery-charging.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   viewBox="0 0 16 16"
   version="1.1"
   id="svg4"
   sodipodi:docname="batterycharge.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <path
     style="fill:#ffffff"
     d="M 1.5878906 4 C 0.7173351 4 1.4802974e-16 4.7173351 0 5.5878906 L 0 10.412109 C 0 11.282665 0.71733509 12 1.5878906 12 L 12.412109 12 C 13.282665 12 14 11.282665 14 10.412109 L 14 10 L 15 10 C 15.546531 10 16 9.5465314 16 9 L 16 7 C 16 6.4534686 15.546531 6 15 6 L 14 6 L 14 5.5878906 C 14 4.7173351 13.282665 4 12.412109 4 L 1.5878906 4 z M 1.5878906 5 L 12.412109 5 C 12.741554 5 13 5.2584462 13 5.5878906 L 13 10.412109 C 13 10.741554 12.741554 11 12.412109 11 L 1.5878906 11 C 1.2584462 11 1 10.741554 1 10.412109 L 1 5.5878906 C 1 5.2584462 1.2584462 5 1.5878906 5 z M 7 6 L 3 8 L 6 8 L 7 10 L 11 8 L 8 8 L 7 6 z M 14 7 L 15 7 L 15 9 L 14 9 L 14 7 z"
     id="path2" />
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/wifi.svg`:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   xmlns:cc="http://creativecommons.org/ns#"
   xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
   xmlns:svg="http://www.w3.org/2000/svg"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
   xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
   version="1.0"
   width="64pt"
   height="64pt"
   viewBox="0 0 64 64"
   preserveAspectRatio="xMidYMid meet"
   id="svg14"
   sodipodi:docname="network.svg"
   inkscape:version="0.92.4 (5da689c313, 2019-01-14)">
  <g
     transform="matrix(0.00400716,0,0,-0.00408343,6.9044127,50.533433)"
     id="g12"
     style="fill:#ffffff;stroke:none">
    <path
       d="M 5905,9230 C 4107,9115 2415,8316 730,6786 639,6703 437,6509 281,6355 l -283,-281 528,-506 529,-506 270,267 c 149,146 335,325 415,397 1869,1685 3702,2322 5595,1943 1358,-271 2797,-1094 4214,-2409 74,-69 158,-146 187,-173 l 52,-47 448,467 c 247,258 475,495 507,529 l 57,61 -173,160 c -1962,1823 -3826,2779 -5767,2958 -194,18 -768,27 -955,15 z"
       id="path4"
       style="fill:#ffffff" />
    <path
       d="M 5945,6613 C 5458,6574 5092,6499 4645,6346 3725,6034 2832,5447 1936,4569 c -125,-122 -226,-225 -226,-229 0,-5 199,-199 442,-431 244,-233 480,-459 525,-503 l 82,-79 189,184 c 300,294 518,484 797,694 642,485 1278,784 1915,899 231,41 342,51 610,51 267,-1 368,-9 610,-51 936,-163 1919,-718 2938,-1658 85,-77 156,-140 158,-139 14,9 1004,1045 1004,1052 0,16 -394,372 -610,552 -967,804 -1919,1320 -2880,1559 -281,70 -549,114 -855,140 -108,9 -591,11 -690,3 z"
       id="path6"
       style="fill:#ffffff" />
    <path
       d="m 6035,4054 c -671,-53 -1337,-336 -1945,-827 -199,-161 -545,-486 -539,-506 2,-5 239,-235 527,-510 l 522,-500 103,101 c 574,564 1131,825 1664,778 326,-29 633,-144 983,-368 180,-115 437,-319 593,-471 l 68,-66 57,60 c 32,33 258,269 502,524 245,255 446,468 447,475 1,6 -49,57 -110,113 -761,692 -1495,1072 -2273,1178 -114,16 -490,27 -599,19 z"
       id="path8"
       style="fill:#ffffff" />
    <path
       d="m 6115,1659 c -299,-43 -565,-236 -689,-500 -55,-116 -70,-187 -70,-324 0,-137 15,-208 70,-324 154,-326 504,-527 884,-507 354,18 652,207 791,502 54,113 72,197 72,329 0,136 -19,220 -78,340 -167,344 -570,543 -980,484 z"
       id="path10"
       style="fill:#ffffff" />
  </g>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/volume.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <path d="M1 6 H4 L8 2.5 V13.5 L4 10 H1 Z" fill="#ffffff"/>
  <path d="M10.3 5.3 A4.2 4.2 0 0 1 10.3 10.7" stroke="#ffffff" stroke-width="1.3" fill="none" stroke-linecap="round"/>
  <path d="M12.3 3 A7.2 7.2 0 0 1 12.3 13" stroke="#ffffff" stroke-width="1.3" fill="none" stroke-linecap="round"/>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/brightness.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <circle cx="8" cy="8" r="3.1" fill="#ffffff"/>
  <g stroke="#ffffff" stroke-width="1.3" stroke-linecap="round">
    <line x1="8" y1="0.6" x2="8" y2="2.6"/>
    <line x1="8" y1="13.4" x2="8" y2="15.4"/>
    <line x1="0.6" y1="8" x2="2.6" y2="8"/>
    <line x1="13.4" y1="8" x2="15.4" y2="8"/>
    <line x1="2.7" y1="2.7" x2="4.1" y2="4.1"/>
    <line x1="11.9" y1="11.9" x2="13.3" y2="13.3"/>
    <line x1="2.7" y1="13.3" x2="4.1" y2="11.9"/>
    <line x1="11.9" y1="4.1" x2="13.3" y2="2.7"/>
  </g>
</svg>
```

### Step 2: Create the title wordmark + 4 button badge source SVGs

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/title.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 220 28">
  <text x="110" y="20" text-anchor="middle" font-family="Arial, Helvetica, sans-serif"
        font-weight="700" font-size="19" letter-spacing="2" fill="#ffffff">CIRCUIT-SWORD</text>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/badge-a.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <mask id="m">
    <rect width="16" height="16" fill="white"/>
    <text x="8" y="11.5" text-anchor="middle" font-family="Arial, Helvetica, sans-serif" font-weight="700" font-size="9" fill="black">A</text>
  </mask>
  <circle cx="8" cy="8" r="7" fill="#ffffff" mask="url(#m)"/>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/badge-b.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <mask id="m">
    <rect width="16" height="16" fill="white"/>
    <text x="8" y="11.5" text-anchor="middle" font-family="Arial, Helvetica, sans-serif" font-weight="700" font-size="9" fill="black">B</text>
  </mask>
  <circle cx="8" cy="8" r="7" fill="#ffffff" mask="url(#m)"/>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/badge-left.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <mask id="m">
    <rect width="16" height="16" fill="white"/>
    <path d="M9.5 4.5 L5.5 8 L9.5 11.5 Z" fill="black"/>
  </mask>
  <circle cx="8" cy="8" r="7" fill="#ffffff" mask="url(#m)"/>
</svg>
```

- [ ] Create `package/batocera/utils/circuitsword-quickmenu/icons/src/badge-right.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <mask id="m">
    <rect width="16" height="16" fill="white"/>
    <path d="M6.5 4.5 L10.5 8 L6.5 11.5 Z" fill="black"/>
  </mask>
  <circle cx="8" cy="8" r="7" fill="#ffffff" mask="url(#m)"/>
</svg>
```

(Each badge's `<mask>` `id="m"` is local to its own SVG file — no collision risk since each file is rasterized independently by the conversion script, never inlined together.)

### Step 3: Add the icon data model to `quickmenu.h`

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`. Find the current icon section (from this morning's earlier plan — starts with the comment `/* qm_icons.c — hand-authored 5x7 bitmap icons...`) and replace the ENTIRE block with:

```c
/* ------------------------------------------------------------------ */
/* qm_icons.c / qm_icon_data.c — real vector-sourced icons, baked to   */
/* fixed-size alpha-only bitmaps by the developer-run                  */
/* tools/convert-icons.py (see that script and icons/src/*.svg -- NOT  */
/* part of the Buildroot build). Every asset renders at its own baked  */
/* pixel size on BOTH panel resolutions -- no runtime scaling. Pure    */
/* drawing, no syscalls, host-testable.                                */
/* ------------------------------------------------------------------ */
#define QM_ICON_SIZE 24   /* width and height of the 9 status icons + 4 badges */
#define QM_TITLE_W   200  /* the title wordmark is wide, not square */
#define QM_TITLE_H   28

typedef enum {
    QM_ICON_BATTERY_EMPTY,
    QM_ICON_BATTERY_25,
    QM_ICON_BATTERY_50,
    QM_ICON_BATTERY_75,
    QM_ICON_BATTERY_FULL,
    QM_ICON_BATTERY_CHARGING,
    QM_ICON_WIFI,
    QM_ICON_VOLUME,
    QM_ICON_BRIGHTNESS,
    QM_ICON_TITLE,
    QM_ICON_BADGE_A,
    QM_ICON_BADGE_B,
    QM_ICON_BADGE_LEFT,
    QM_ICON_BADGE_RIGHT,
    QM_ICON_COUNT
} qm_icon_kind;

typedef struct {
    int width;
    int height;
    const uint8_t *alpha;   /* width * height bytes, row-major, one byte
                                per pixel, 0 = transparent, 255 = fully
                                the draw color */
} qm_icon_asset;

/* Defined in the generated qm_icon_data.c (tools/convert-icons.py). */
extern const qm_icon_asset qm_icon_assets[QM_ICON_COUNT];

/* Pure, no I/O: (bg, fg, alpha) -> blended channel value. Exposed for
 * direct unit testing of the blend math, independent of any real icon
 * data -- see tests/test_qm_icons.c. */
uint8_t qm_alpha_blend(uint8_t bg, uint8_t fg, uint8_t alpha);

/* Alpha-blends `color` over the framebuffer at (x,y) using the icon's
 * own baked width/height alpha mask -- no runtime scaling, every asset
 * draws at its exact baked pixel size. Out-of-range kind no-ops (same
 * defensive default the prior bitmap version had). */
void qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color);

/* Pure, no I/O. charging always wins regardless of percent. Boundaries:
 * <10 -> EMPTY, <35 -> 25, <60 -> 50, <85 -> 75, else -> FULL. */
qm_icon_kind qm_battery_icon_kind(int percent, int charging);
```

### Step 4: Write `tools/convert-icons.py`

- [ ] Create `/Users/bas/batocera-build-wifi/batocera.linux/tools/convert-icons.py`:

```python
#!/usr/bin/env python3
"""Rasterizes the SVG icons in
package/batocera/utils/circuitsword-quickmenu/icons/src/ into
package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c.

Developer-run only -- NOT part of the Buildroot build, NOT invoked by any
.mk file. Re-run whenever a source icon is added or replaced, then commit
the diff to qm_icon_data.c alongside the source SVG change.

Requires (one-time, on the developer's machine):
    pip install cairosvg pillow
"""
import io
import os

import cairosvg
from PIL import Image

ICON_SIZE = 24
TITLE_W = 200
TITLE_H = 28

# (C enum member name, source SVG filename in icons/src/, width, height)
ICONS = [
    ("QM_ICON_BATTERY_EMPTY", "battery-empty.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BATTERY_25", "battery-25.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BATTERY_50", "battery-50.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BATTERY_75", "battery-75.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BATTERY_FULL", "battery-full.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BATTERY_CHARGING", "battery-charging.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_WIFI", "wifi.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_VOLUME", "volume.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BRIGHTNESS", "brightness.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_TITLE", "title.svg", TITLE_W, TITLE_H),
    ("QM_ICON_BADGE_A", "badge-a.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BADGE_B", "badge-b.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BADGE_LEFT", "badge-left.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_BADGE_RIGHT", "badge-right.svg", ICON_SIZE, ICON_SIZE),
]

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(
    REPO_ROOT, "package/batocera/utils/circuitsword-quickmenu/icons/src")
OUT_FILE = os.path.join(
    REPO_ROOT, "package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c")


def rasterize_alpha(svg_path, width, height):
    png_bytes = cairosvg.svg2png(
        url=svg_path, output_width=width, output_height=height)
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    if img.size != (width, height):
        raise ValueError(
            f"{svg_path}: rasterized to {img.size}, expected "
            f"({width}, {height})")
    return [img.getpixel((x, y))[3]
            for y in range(height) for x in range(width)]


def main():
    lines = [
        "/* Generated by tools/convert-icons.py -- DO NOT EDIT BY HAND.",
        " * Re-run the script and commit the diff to update an icon. */",
        '#include "quickmenu.h"',
        "",
    ]

    for name, filename, width, height in ICONS:
        alpha = rasterize_alpha(os.path.join(SRC_DIR, filename), width, height)
        var = f"qm_icon_alpha_{name.lower()}"
        lines.append(f"static const uint8_t {var}[{width * height}] = {{")
        for row in range(height):
            row_vals = alpha[row * width:(row + 1) * width]
            lines.append("    " + ",".join(str(v) for v in row_vals) + ",")
        lines.append("};")
        lines.append("")

    lines.append("const qm_icon_asset qm_icon_assets[QM_ICON_COUNT] = {")
    for name, _, width, height in ICONS:
        var = f"qm_icon_alpha_{name.lower()}"
        lines.append(f"    [{name}] = {{ {width}, {height}, {var} }},")
    lines.append("};")
    lines.append("")

    with open(OUT_FILE, "w") as f:
        f.write("\n".join(lines))

    print(f"wrote {OUT_FILE} ({len(ICONS)} icons)")


if __name__ == "__main__":
    main()
```

### Step 5: Install dependencies and run the script

- [ ] Run:
  ```bash
  pip install cairosvg pillow
  ```
  Expected: both packages install successfully (or report already-satisfied).

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  python3 tools/convert-icons.py
  ```
  Expected: `wrote .../qm_icon_data.c (14 icons)`, no errors. If the title/badge SVGs fail to rasterize because `<mask>`/`<text>` support is missing in the installed CairoSVG version, upgrade it (`pip install --upgrade cairosvg`) — both features are supported in modern CairoSVG releases.

- [ ] Verify the generated file looks sane:
  ```bash
  grep -c "^static const uint8_t qm_icon_alpha_" "package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c"
  grep -c "^const qm_icon_asset qm_icon_assets" "package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c"
  grep -c "QM_ICON_TITLE.*200, 28" "package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c"
  ```
  Expected: `14`, `1`, `1`.

### Step 6: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/icons/src/ \
          package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c \
          package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
          tools/convert-icons.py
  git commit -m "circuitsword-quickmenu: add real vector icon/title/badge sources + baked qm_icon_data.c"
  ```

---

## Task 2: Rewrite `qm_icons.c` around the alpha-blend primitive

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes (from Task 1): `QM_ICON_SIZE`, `QM_TITLE_W`/`QM_TITLE_H`, the 14-member `qm_icon_kind` enum, `qm_icon_asset` (with `width`/`height`), `qm_icon_assets[]` — all in `quickmenu.h`.
- Produces: `qm_alpha_blend()`, `qm_draw_icon_rgba()`, `qm_battery_icon_kind()` — declared in `quickmenu.h` by Task 1; this task defines them.

### Step 1: Read the current file fresh, then replace it entirely

- [ ] Read `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c` to confirm its current content matches the pixel-art implementation from this morning. Replace the entire file with:

```c
/* Pure drawing into a 32bpp XRGB8888 buffer. No syscalls -- this file is
 * compiled by the host compiler in tests/run-c-tests.sh as well as by the
 * Buildroot cross toolchain.
 *
 * Icons: real vector-sourced art (Batocera/EmulationStation's own battery
 * and WiFi icons, plus newly hand-drawn icons for volume/brightness, the
 * title wordmark, and the four button badges, all in the same flat-
 * silhouette style), each baked to its own fixed width/height alpha mask
 * by the developer-run tools/convert-icons.py -- see icons/src/*.svg and
 * the generated qm_icon_data.c. This file only does the alpha-blend blit
 * and the battery-state mapping; it has no bitmap data of its own. See
 * docs/superpowers/specs/2026-08-10-real-overlay-icons-design.md
 * (including its title/badge addendum). */
#include "quickmenu.h"

uint8_t qm_alpha_blend(uint8_t bg, uint8_t fg, uint8_t alpha)
{
    return (uint8_t)(bg + ((int)fg - (int)bg) * alpha / 255);
}

void qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color)
{
    if (kind < 0 || kind >= QM_ICON_COUNT)
        return;

    const qm_icon_asset *asset = &qm_icon_assets[kind];
    const uint8_t *alpha = asset->alpha;
    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    for (int row = 0; row < asset->height; row++) {
        for (int col = 0; col < asset->width; col++) {
            uint8_t a = alpha[row * asset->width + col];
            if (a == 0)
                continue;

            uint32_t bg = qm_get_pixel(fb, x + col, y + row);
            uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
            uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
            uint8_t bb = (uint8_t)(bg & 0xFF);

            uint32_t blended = QM_RGB(
                qm_alpha_blend(br, cr, a),
                qm_alpha_blend(bgc, cg, a),
                qm_alpha_blend(bb, cb, a));
            qm_fill_rect(fb, x + col, y + row, 1, 1, blended);
        }
    }
}

qm_icon_kind qm_battery_icon_kind(int percent, int charging)
{
    if (charging)
        return QM_ICON_BATTERY_CHARGING;
    if (percent < 10)
        return QM_ICON_BATTERY_EMPTY;
    if (percent < 35)
        return QM_ICON_BATTERY_25;
    if (percent < 60)
        return QM_ICON_BATTERY_50;
    if (percent < 85)
        return QM_ICON_BATTERY_75;
    return QM_ICON_BATTERY_FULL;
}
```

### Step 2: Rewrite `tests/test_qm_icons.c`

- [ ] Read `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c` to confirm its current content tests the old fixed-size API. Replace the entire file with:

```c
/* Host-side unit tests for circuitsword-quickmenu's real-icon drawing
 * code (qm_icons.c) and the generated icon data (qm_icon_data.c).
 * Compiled with the host cc against the real source -- no Wayland, no
 * evdev, no device needed. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickmenu.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

static qm_fb *make_fb(uint32_t w, uint32_t h)
{
    qm_fb *fb = malloc(sizeof(*fb));
    fb->width  = w;
    fb->height = h;
    fb->pitch  = w * 4;
    fb->pixels = calloc((size_t)fb->pitch * h, 1);
    return fb;
}

static void free_fb(qm_fb *fb) { free(fb->pixels); free(fb); }

static int count_nonzero(const qm_fb *fb)
{
    int n = 0;
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            if (qm_get_pixel(fb, (int)x, (int)y) != 0) n++;
    return n;
}

int main(void)
{
    printf("qm_alpha_blend\n");
    {
        check(qm_alpha_blend(10, 200, 0) == 10, "alpha 0 leaves background untouched");
        check(qm_alpha_blend(10, 200, 255) == 200, "alpha 255 becomes exactly fg");
        uint8_t half = qm_alpha_blend(0, 254, 128);
        check(half >= 120 && half <= 132, "alpha 128 lands roughly halfway (0->254)");
    }

    printf("qm_battery_icon_kind boundaries\n");
    {
        check(qm_battery_icon_kind(0, 0) == QM_ICON_BATTERY_EMPTY, "0%% -> EMPTY");
        check(qm_battery_icon_kind(9, 0) == QM_ICON_BATTERY_EMPTY, "9%% -> EMPTY");
        check(qm_battery_icon_kind(10, 0) == QM_ICON_BATTERY_25, "10%% -> 25");
        check(qm_battery_icon_kind(34, 0) == QM_ICON_BATTERY_25, "34%% -> 25");
        check(qm_battery_icon_kind(35, 0) == QM_ICON_BATTERY_50, "35%% -> 50");
        check(qm_battery_icon_kind(59, 0) == QM_ICON_BATTERY_50, "59%% -> 50");
        check(qm_battery_icon_kind(60, 0) == QM_ICON_BATTERY_75, "60%% -> 75");
        check(qm_battery_icon_kind(84, 0) == QM_ICON_BATTERY_75, "84%% -> 75");
        check(qm_battery_icon_kind(85, 0) == QM_ICON_BATTERY_FULL, "85%% -> FULL");
        check(qm_battery_icon_kind(100, 0) == QM_ICON_BATTERY_FULL, "100%% -> FULL");
        check(qm_battery_icon_kind(50, 1) == QM_ICON_BATTERY_CHARGING,
              "charging always wins regardless of percent");
        check(qm_battery_icon_kind(0, 1) == QM_ICON_BATTERY_CHARGING,
              "charging wins even at 0%%");
    }

    printf("qm_draw_icon_rgba draws something for every real icon\n");
    {
        qm_icon_kind cases[] = {
            QM_ICON_BATTERY_EMPTY, QM_ICON_BATTERY_25, QM_ICON_BATTERY_50,
            QM_ICON_BATTERY_75, QM_ICON_BATTERY_FULL, QM_ICON_BATTERY_CHARGING,
            QM_ICON_WIFI, QM_ICON_VOLUME, QM_ICON_BRIGHTNESS, QM_ICON_TITLE,
            QM_ICON_BADGE_A, QM_ICON_BADGE_B, QM_ICON_BADGE_LEFT, QM_ICON_BADGE_RIGHT,
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const qm_icon_asset *asset = &qm_icon_assets[cases[i]];
            qm_fb *fb = make_fb((uint32_t)asset->width, (uint32_t)asset->height);
            qm_draw_icon_rgba(fb, 0, 0, cases[i], QM_RGB(0xFF, 0xFF, 0xFF));
            check(count_nonzero(fb) > 0, "icon draws at least one pixel");
            free_fb(fb);
        }
    }

    printf("qm_draw_icon_rgba respects each asset's own width/height\n");
    {
        check(qm_icon_assets[QM_ICON_TITLE].width == QM_TITLE_W &&
              qm_icon_assets[QM_ICON_TITLE].height == QM_TITLE_H,
              "title asset is QM_TITLE_W x QM_TITLE_H, not QM_ICON_SIZE");
        check(qm_icon_assets[QM_ICON_BADGE_A].width == QM_ICON_SIZE &&
              qm_icon_assets[QM_ICON_BADGE_A].height == QM_ICON_SIZE,
              "badge asset is QM_ICON_SIZE square");
    }

    printf("qm_draw_icon_rgba distinguishes states\n");
    {
        qm_fb *a = make_fb(QM_ICON_SIZE, QM_ICON_SIZE), *b = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(a, 0, 0, QM_ICON_BATTERY_EMPTY, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon_rgba(b, 0, 0, QM_ICON_BATTERY_FULL, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "empty battery differs from full battery");
        free_fb(a); free_fb(b);

        qm_fb *c = make_fb(QM_ICON_SIZE, QM_ICON_SIZE), *d = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(c, 0, 0, QM_ICON_BADGE_A, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon_rgba(d, 0, 0, QM_ICON_BADGE_B, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(c->pixels, d->pixels, (size_t)c->pitch * c->height) != 0,
              "badge A differs from badge B");
        free_fb(c); free_fb(d);
    }

    printf("qm_draw_icon_rgba out-of-range kind no-ops instead of crashing\n");
    {
        qm_fb *fb = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(fb, 0, 0, (qm_icon_kind)-1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "negative kind draws nothing, no crash");
        qm_draw_icon_rgba(fb, 0, 0, QM_ICON_COUNT, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "QM_ICON_COUNT (one past last) draws nothing, no crash");
        free_fb(fb);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

### Step 3: Update `run-c-tests.sh`'s `test_qm_icons` compile line

- [ ] Read `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh` to confirm its current `test_qm_icons` block. Find:
  ```bash
  cc -std=gnu99 -O1 -Wall -Wextra -Werror \
     -DQM_NO_MAIN \
     -I"$QM_SRC" \
     "$HERE/test_qm_icons.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_font.c" "$QM_SRC/quickmenu.c" \
     -o "$OUT/test_qm_icons"
  ```
  Replace with (add `qm_icon_data.c`; `qm_font.c`/`quickmenu.c` are no longer needed by this test since it no longer exercises `qm_render()` or the font, only `qm_icons.c` + the generated data):
  ```bash
  cc -std=gnu99 -O1 -Wall -Wextra -Werror \
     -DQM_NO_MAIN \
     -I"$QM_SRC" \
     "$HERE/test_qm_icons.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_icon_data.c" "$QM_SRC/qm_font.c" \
     -o "$OUT/test_qm_icons"
  ```
  (`qm_font.c` stays in the compile line because `qm_fill_rect`/`qm_get_pixel`, used by `qm_draw_icon_rgba`, are defined there.)

### Step 4: Run the tests

- [ ] Run just this task's own test in isolation (the rest of `run-c-tests.sh` still references the OLD API in `quickmenu.c`/`sb_render.c`, which is Task 3's job):
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu
  cc -std=gnu99 -O1 -Wall -Wextra -Werror -DQM_NO_MAIN -I. \
     "/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c" qm_icons.c qm_icon_data.c qm_font.c \
     -o /tmp/test_qm_icons_only
  /tmp/test_qm_icons_only
  ```
  Expected: `PASSED (0 failures)`. The full `run-c-tests.sh` will pass end-to-end once Task 3 updates `quickmenu.c`/`sb_render.c` and their test files.

### Step 5: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/qm_icons.c
  git commit -m "circuitsword-quickmenu: rewrite qm_icons.c around real-icon alpha blend"
  ```
  (Only `qm_icons.c` changes in the batocera.linux repo this step — the test file and `run-c-tests.sh` live in the main project directory, which has no git repo, and are saved directly per the Global Constraints.)

---

## Task 3: Wire icons/title/badges into both overlays, layout change, Buildroot verification

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-REAL-ICONS-FINDINGS.md`

**Interfaces:**
- Consumes (from Task 2): `qm_draw_icon_rgba()`, `qm_battery_icon_kind()`, `QM_ICON_SIZE`, `QM_TITLE_W`, `qm_icon_assets[]`, and the enum members `QM_ICON_WIFI`/`QM_ICON_VOLUME`/`QM_ICON_BRIGHTNESS`/`QM_ICON_TITLE`/`QM_ICON_BADGE_A`/`QM_ICON_BADGE_B`/`QM_ICON_BADGE_LEFT`/`QM_ICON_BADGE_RIGHT` — all in `quickmenu.h`.

### Step 1: Update `quickmenu.c` — icons, same-row layout, title, hint-line badges

- [ ] Read `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.c` to confirm its current `qm_render()` (calls the OLD fixed-size icon API, draws the title/hint via `qm_draw_text`, stacked icon-above/bar-below layout). Replace `qm_render()` (the whole function) with:

```c
void qm_render(qm_fb *fb, const qm_state *st)
{
    const int scale = (fb->width >= 640) ? 4 : 2;
    const int margin = 8 * scale;
    const int gap = 2 * scale;
    const int bar_h = 3 * scale;
    const int bar_x = margin + QM_ICON_SIZE + gap;
    const int bar_w = (int)fb->width - margin - bar_x;
    const int row_h = QM_ICON_SIZE + 2 * scale;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    int title_x = ((int)fb->width - QM_TITLE_W) / 2;
    qm_draw_icon_rgba(fb, title_x, margin, QM_ICON_TITLE, QM_COLOR_DIM);

    int y = margin + QM_TITLE_H + QM_TITLE_H / 2;

    for (int item = 0; item < QM_ITEM_COUNT; item++) {
        if (item == st->selected)
            qm_fill_rect(fb, margin / 2, y - scale,
                         (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);

        uint32_t fg = (item == st->selected) ? QM_COLOR_FG : QM_COLOR_DIM;

        if (item == QM_ITEM_WIFI) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_WIFI,
                               st->wifi_on ? QM_COLOR_FG : QM_COLOR_DIM);
        } else if (item == QM_ITEM_VOLUME) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_VOLUME, fg);
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w, bar_h,
                        st->volume);
        } else {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_BRIGHTNESS, fg);
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w, bar_h,
                        st->brightness);
        }
        y += row_h + QM_ICON_SIZE / 2;
    }

    /* Hint line: badge + label, badge + label, badge-pair + label,
     * centered as one row. Compute the total width first so the whole
     * line can be centered, matching what qm_draw_text(hint, ...) used
     * to do via qm_text_width(). */
    const char *lbl_select = "SELECT";
    const char *lbl_back = "BACK";
    const char *lbl_adjust = "ADJUST";
    int hint_gap = 3 * 1;      /* small gap between a badge and its label, scale 1 */
    int group_gap = 14;        /* gap between hint groups */
    int total_w =
        QM_ICON_SIZE + hint_gap + qm_text_width(lbl_select, 1) + group_gap +
        QM_ICON_SIZE + hint_gap + qm_text_width(lbl_back, 1) + group_gap +
        QM_ICON_SIZE + QM_ICON_SIZE + hint_gap + qm_text_width(lbl_adjust, 1);

    int hx = ((int)fb->width - total_w) / 2;
    int hy = (int)fb->height - margin - QM_ICON_SIZE;
    int text_y = hy + (QM_ICON_SIZE - QM_GLYPH_H) / 2;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_A, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text(fb, hx, text_y, lbl_select, 1, QM_COLOR_DIM);
    hx += qm_text_width(lbl_select, 1) + group_gap;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_B, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text(fb, hx, text_y, lbl_back, 1, QM_COLOR_DIM);
    hx += qm_text_width(lbl_back, 1) + group_gap;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_LEFT, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE;
    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_RIGHT, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text(fb, hx, text_y, lbl_adjust, 1, QM_COLOR_DIM);
}
```

  Notes on this rewrite (verify against the file you actually read, adjust only if it differs from what's described above, not if it differs from this note):
  - `qm_draw_bar()` (the small helper just above `qm_render()`) is untouched — same signature, only the arguments passed to it change.
  - WiFi's icon color switches between `QM_COLOR_FG` (on) and `QM_COLOR_DIM` (off) regardless of row selection, so its state stays visible no matter which row is highlighted — matching the existing convention already established for this row. Volume/brightness icons use the row's `fg` (dim when unselected), same as before.
  - The title now draws via `qm_draw_icon_rgba` instead of `qm_draw_text` — remove the old `qm_text_width(title, scale)`-based centering line entirely, it's replaced by `title_x` computed from `QM_TITLE_W`.
  - Row vertical spacing (`y`) now derives from `QM_TITLE_H` and `QM_ICON_SIZE`, not `line_h` (which was `(QM_GLYPH_H + 4) * scale` in the old version) — `line_h` as a variable is no longer used anywhere in this function and should not be left declared unused.

### Step 2: Update `sb_render.c`

- [ ] Read `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c` to confirm its current content (draws icons via the old fixed-size API). Replace the entire file with:

```c
/* Pure drawing for circuitsword-statusbar: battery, WiFi, volume,
 * brightness laid out left-to-right across a thin top bar. No I/O, no
 * Wayland -- reuses circuitsword-quickmenu's qm_icons.c/qm_icon_data.c
 * primitives and QM_COLOR_* palette so the bar matches the same look as
 * the quickmenu overlay. Host-unit-tested in tests/test_sb_render.c. */
#include "statusbar.h"

void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging)
{
    /* Four fixed-size (QM_ICON_SIZE px) icons, same on both panels (no
     * runtime scaling) -- worst case is the same for every reading (no
     * variable-width text/percentage to budget for). margin(6) +
     * 4*QM_ICON_SIZE(96) + 3*gap(36) = 6+96+36 = 138px, comfortably
     * under both the 640px primary and 320px alt panel widths. */
    const int margin = 6;
    const int gap = 12;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    int y = ((int)fb->height - QM_ICON_SIZE) / 2;
    int x = margin;

    qm_draw_icon_rgba(fb, x, y, qm_battery_icon_kind(battery_percent, battery_charging),
                       QM_COLOR_FG);
    x += QM_ICON_SIZE + gap;

    qm_draw_icon_rgba(fb, x, y, QM_ICON_WIFI, wifi_on ? QM_COLOR_FG : QM_COLOR_DIM);
    x += QM_ICON_SIZE + gap;

    qm_draw_icon_rgba(fb, x, y, QM_ICON_VOLUME, QM_COLOR_FG);
    x += QM_ICON_SIZE + gap;

    qm_draw_icon_rgba(fb, x, y, QM_ICON_BRIGHTNESS, QM_COLOR_FG);
}
```

### Step 3: Update `test_sb_render.c`

- [ ] Read `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c` to confirm its current content. Its existing assertions (background painted, charging-vs-not differs, wifi-on-vs-off differs, extreme values don't crash, worst-case-width regression on the 320px panel) all still apply conceptually against the new `sb_render()` — same signature, same "pixel output differs by state" style of check, nothing API-specific to the old fixed-scale icon system. No code change should be needed in this file; run the test suite (Step 5 below) to confirm it still compiles and passes as-is. If it does NOT compile, fix only what's broken, preserving every existing check's intent.

### Step 4: Update both `.mk` files

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`. Find `CIRCUITSWORD_QUICKMENU_SRCS` (currently includes `qm_icons.c` from this morning's earlier plan) and add `qm_icon_data.c` immediately after it:
  ```makefile
  CIRCUITSWORD_QUICKMENU_SRCS = \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/quickmenu.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_icon_data.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_icons.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_input.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_settings.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl.c
  ```
  (Verify against the file's actual current content before editing — order shown is alphabetical, matching the existing convention.)

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`. Find `CIRCUITSWORD_STATUSBAR_SRCS` (currently includes `qm_icons.c` via `CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR`) and add `qm_icon_data.c` from the same shared directory:
  ```makefile
  CIRCUITSWORD_STATUSBAR_SRCS = \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/statusbar.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_render.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_wl.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_font.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_icon_data.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_icons.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_settings.c
  ```

### Step 5: Run the full host test suite

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: all four binaries (`test_qm_font`, `test_qm_icons`, `test_qm_settings`, `test_sb_render`) compile and print `PASSED (0 failures)`. `test_qm_font`'s compile line compiles `quickmenu.c` itself, which now calls `qm_draw_icon_rgba`/`qm_battery_icon_kind`/references `qm_icon_assets` — confirm its compile line already includes `qm_icon_data.c`; if not, add `"$QM_SRC/qm_icon_data.c"` to that block the same way Task 2 Step 3 did for `test_qm_icons`.

### Step 6: Incremental Buildroot compile-check for both packages

- [ ] Run:
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -40
  '
  ```
  Expected: both `wayland-scanner` invocations, then the `cc` invocation compiling `quickmenu.c`, `qm_font.c`, `qm_icon_data.c`, `qm_icons.c`, `qm_input.c`, `qm_settings.c`, `qm_wl.c` together, with no errors, ending in the generated `circuitsword-quickmenu` binary.

- [ ] Run the same pattern for the status bar package:
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-statusbar-rebuild 2>&1 | tail -40
  '
  ```
  Expected: no errors, ending in the generated `circuitsword-statusbar` binary.

### Step 7: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c \
          package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk \
          package/batocera/utils/circuitsword-statusbar/sb_render.c \
          package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk
  git commit -m "circuitsword-quickmenu, circuitsword-statusbar: wire in real icons/title/badges, same-row layout"
  ```

### Step 8: Regenerate the patch capture

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- [ ] Verify the new code is present:
  ```bash
  grep -c "qm_draw_icon_rgba" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "qm_battery_icon_kind" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "QM_ICON_BADGE_A" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "qm_icon_alpha_qm_icon_title" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
  Expected: all four counts greater than 0 (the last confirms the generated title baked-data made it into the diff, not just the hand-written code).

### Step 9: Write the findings log

- [ ] Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-REAL-ICONS-FINDINGS.md`:

```markdown
# Real Overlay Icons: Findings

Implements `docs/superpowers/specs/2026-08-10-real-overlay-icons-design.md`
(including its title/badge addendum). Replaces this morning's hand-
authored 5x7 pixel-art icons AND the quickmenu's bitmap-font title/hint
line with real vector-sourced art (battery/WiFi from Batocera/
EmulationStation's own icons; volume/brightness/title/button-badges newly
hand-drawn to match), each baked to its own fixed pixel size by a
developer-run conversion script. Also moves the quickmenu's icon and
percentage bar onto one row instead of two.

## What was built

- `tools/convert-icons.py` (new, developer-run, NOT part of the Buildroot
  build): rasterizes 14 source SVGs (9 status icons, 1 title wordmark, 4
  button badges) to fixed-size alpha-only bitmaps via CairoSVG, emitting
  the generated-but-committed `qm_icon_data.c`.
- `qm_icons.c` (rewritten): `qm_alpha_blend()` (pure per-channel blend
  math), `qm_draw_icon_rgba()` (alpha-over blit, reads each asset's own
  width/height -- no fixed size assumption, no runtime scaling by panel),
  `qm_battery_icon_kind()` (percent+charging -> one of 6 battery states).
- `quickmenu.h`: `qm_icon_kind` grew to 14 members; `qm_icon_asset` gained
  `width`/`height` fields (previously would have assumed one fixed size).
- `quickmenu.c`: title now draws via the baked wordmark instead of the
  bitmap font; hint line now shows colored button badges (A/B/LEFT/RIGHT)
  instead of plain "A:"/"B:"/"LEFT/RIGHT:" text prefixes; volume/
  brightness icon and their percentage bar now sit on the same row,
  vertically centered, instead of icon-above/bar-below.
- `sb_render.c`: all four status-bar icons now draw via the new real-icon
  API; battery uses the new 6-state mapping instead of a generic 0-3
  level; icons render at a fixed size on both panels (no `* scale`).

## Verified off-device

- `tests/test_qm_icons.c`: `qm_alpha_blend()`'s boundary math,
  `qm_battery_icon_kind()`'s full boundary set, all 14 real icons draw at
  least one non-background pixel, the title asset is confirmed to use
  `QM_TITLE_W`x`QM_TITLE_H` rather than the square `QM_ICON_SIZE`,
  distinguishable pixel output between states, out-of-range kind no-ops.
- `tests/test_sb_render.c`'s existing tests still pass against the new
  draw calls.
- `tests/test_qm_font.c`'s existing `qm_render()` tests still pass
  against the new icon+bar-same-row layout and the new title/hint-line
  drawing (they only assert "draws something" and background color).
- Incremental Buildroot compiles (`PKG=circuitsword-quickmenu-rebuild`
  and `PKG=circuitsword-statusbar-rebuild`) both succeed.
- Patch capture confirmed to contain the new code, including the
  generated baked-icon/title data, via targeted `grep`.

## Needs on-device validation (not yet done)

- **Visual quality**: the whole point of this change -- real vector-
  sourced icons, a sharp title wordmark, and colored button badges should
  read far more clearly than the previous pixel art/plain text, but this
  has only been confirmed via a rendered HTML preview (shown to and
  approved by the user before implementation), not on the actual DPI
  panel.
- **Layout proportions**: the quickmenu's new icon+bar-same-row layout
  and hint-line badge spacing, plus the status bar's fixed-size (no
  per-panel scaling) icon layout, need confirming on the real panels.
- **Title/badge font dependency**: the title and badge SVGs use `<text>`
  elements rendered by whatever font was installed on the machine that
  ran `tools/convert-icons.py` -- confirm the baked result looks correct;
  if it doesn't, the fix is re-running the script with a different font
  available, not a code change.
```

---
