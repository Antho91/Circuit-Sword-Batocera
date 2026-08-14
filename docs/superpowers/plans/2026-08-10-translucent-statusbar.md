# Translucent Status Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `circuitsword-statusbar`'s background uniformly 80% opaque instead of fully opaque black, so the running game stays partially visible through the bar strip, while its icons (battery/WiFi/volume/brightness) stay fully opaque and legible.

**Architecture:** Add a `QM_ARGB` color macro alongside the existing alpha-less `QM_RGB`. `qm_draw_icon_rgba()` (shared by both overlays) is changed to always write full opacity (alpha=255) for any pixel it draws — a universal, harmless change for `circuitsword-quickmenu`, which stays on an alpha-ignoring `XRGB8888` surface. `sb_render.c`'s plain background fill switches to a translucent color. `sb_wl.c` switches its `wl_shm` buffer to `ARGB8888` and drops the "this surface is opaque" hint to the compositor, so labwc actually blends it.

**Tech Stack:** C (gnu99, no toolkit, same as the rest of both overlay packages), host `cc` for tests (`tests/run-c-tests.sh`), Buildroot `generic-package`.

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-translucent-statusbar-design.md` — this plan implements it exactly; do not deviate from its architecture (uniform 80%, icons stay fully opaque, `circuitsword-quickmenu` untouched) or scope.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`), currently at `HEAD` after this session's `real-overlay-icons` plan (commit `be022be577`). File changes happen here and get real git commits in this repo.
- **`circuitsword-quickmenu`'s own `qm_wl.c` is explicitly OUT OF SCOPE** — it stays `WL_SHM_FORMAT_XRGB8888` and fully opaque, by original, still-valid design (a full-screen interactive menu that intentionally occludes everything underneath while open). Do not touch it, and do not "harmonize" it with `sb_wl.c` — the two are now deliberately different formats on purpose.
- Opacity value: `204` (`= round(0.8 * 255)`), used as the alpha byte for `sb_render.c`'s background fill. Every task uses this exact value.
- **`batocera-build/scripts/*.sh` (`build-image.sh`, `env.sh`) default `BATOCERA_SRC` to a stale, unpatched checkout** at `batocera-build/build/batocera.linux` inside the main project directory — NOT where this project's development happens. Any command that sources `env.sh` or invokes `make` in the build tree MUST explicitly `export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux` first. `env.sh` itself requires `bash`, not `zsh` — wrap in `bash -c '...'` if the shell is zsh. Do not use `batocera-build/scripts/build-image.sh` for this plan's verification steps — it triggers a multi-hour full image build, which this plan does not need.
- Per Hard Rule #7 in the project's root `CLAUDE.md` (Buildroot incremental-build staleness gotcha): `circuitsword-quickmenu` and `circuitsword-statusbar` are both real compiled packages — use `PKG=circuitsword-quickmenu-rebuild` / `PKG=circuitsword-statusbar-rebuild` (not `-reinstall`) for verification.
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- After the build-tree changes are committed, regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- No hardware in CI. Testing is off-device only: host `cc`-compiled C tests (`tests/run-c-tests.sh`) and incremental Buildroot single-package compiles (`bcm2837-pkg PKG=<name>-rebuild`). Whether labwc actually honors `ARGB8888` translucency on a `wlr-layer-shell` surface is genuinely new ground on this hardware — tracked in the findings log, never claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md` (created in this task).

---

## Task 1: Translucent background, opaque icons, ARGB8888 buffer

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_wl.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md`

**Interfaces:**
- Produces: `QM_ARGB(a, r, g, b)` macro in `quickmenu.h`, alongside the existing `QM_RGB(r, g, b)`. `qm_draw_icon_rgba()`'s output pixels now always carry alpha=255 in their top byte (previously alpha=0, since it used `QM_RGB` which never sets that byte). `sb_render()`'s background pixels now carry alpha=204 (previously alpha=0, opaque-by-convention).

### Step 1: Add the `QM_ARGB` macro to `quickmenu.h`

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`. Find the existing `QM_RGB` macro (currently lines 20-21):
  ```c
  #define QM_RGB(r, g, b) \
      (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
  ```
  Add `QM_ARGB` immediately after it:
  ```c
  #define QM_RGB(r, g, b) \
      (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

  #define QM_ARGB(a, r, g, b) \
      (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
  ```
  Do not modify `QM_RGB` itself or any of the `QM_COLOR_*` constants defined below it (`QM_COLOR_BG`, `QM_COLOR_FG`, etc.) — they stay exactly as they are, still alpha=0 in their top byte, still correct for every existing `XRGB8888` caller.

### Step 2: Make `qm_draw_icon_rgba()` always write full opacity

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c`. Find the blended-pixel construction inside `qm_draw_icon_rgba()` (currently lines 43-47):
  ```c
              uint32_t blended = QM_RGB(
                  qm_alpha_blend(br, cr, a),
                  qm_alpha_blend(bgc, cg, a),
                  qm_alpha_blend(bb, cb, a));
              qm_fill_rect(fb, x + col, y + row, 1, 1, blended);
  ```
  Replace `QM_RGB` with `QM_ARGB(255, ...)`:
  ```c
              uint32_t blended = QM_ARGB(255,
                  qm_alpha_blend(br, cr, a),
                  qm_alpha_blend(bgc, cg, a),
                  qm_alpha_blend(bb, cb, a));
              qm_fill_rect(fb, x + col, y + row, 1, 1, blended);
  ```
  This is the only change to this file. The rest of `qm_draw_icon_rgba()`, `qm_alpha_blend()`, and `qm_battery_icon_kind()` are untouched.

### Step 3: Write the failing tests for full-opacity icon pixels

- [ ] Open `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c`. Add a new test block after the existing `"qm_draw_icon_rgba out-of-range kind no-ops instead of crashing"` block (i.e. as the new last block, right before the final `printf("\n%s (%d failure%s)\n", ...)` summary line):
  ```c
      printf("qm_draw_icon_rgba writes full opacity for every pixel it touches\n");
      {
          qm_fb *fb = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
          qm_draw_icon_rgba(fb, 0, 0, QM_ICON_BATTERY_FULL, QM_RGB(0xFF, 0xFF, 0xFF));
          int bad_alpha = 0;
          for (uint32_t y = 0; y < fb->height; y++)
              for (uint32_t x = 0; x < fb->width; x++) {
                  uint32_t p = qm_get_pixel(fb, (int)x, (int)y);
                  if (p != 0 && ((p >> 24) & 0xFF) != 255)
                      bad_alpha++;
              }
          check(bad_alpha == 0,
                "every drawn pixel has alpha=255, none partially transparent");
          free_fb(fb);
      }
  ```
  (The draw color passed in, `QM_RGB(0xFF, 0xFF, 0xFF)`, itself has alpha=0 in its own top byte — this test proves `qm_draw_icon_rgba` ignores the input color's alpha bits and always writes 255 to the output, regardless of what was passed in.)

### Step 4: Run the tests to verify this one fails, everything else still passes

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: `test_qm_icons` FAILS on the new "every drawn pixel has alpha=255" check (still alpha=0, since Step 2 hasn't landed the source change into the running binary yet — wait, Step 2 above already edited the source; if you're following these steps in order, this run should actually PASS immediately since Step 2 already applied the fix. If you deliberately want to see it fail first, temporarily revert Step 2's `qm_icons.c` edit, run this, confirm the new check fails, then re-apply Step 2 and re-run to confirm it passes. Either order is fine — the important thing is confirming the new test actually exercises the new behavior, not that it was literally red before green.)

### Step 5: Make `sb_render.c`'s background translucent

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`. Find the background fill line (currently line 26):
  ```c
      qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
  ```
  Replace with:
  ```c
      qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_ARGB(204, 0, 0, 0));
  ```
  Nothing else in this file changes — the four `qm_draw_icon_rgba(...)` calls below it are untouched (they already independently guarantee full icon opacity as of Step 2).

### Step 6: Switch `sb_wl.c` to an `ARGB8888` buffer and drop the opaque-region hint

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_wl.c`. Find the buffer format at the `wl_shm_pool_create_buffer` call (currently lines 173-175):
  ```c
      w->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)w->width,
                                            (int32_t)w->height, (int32_t)stride,
                                            WL_SHM_FORMAT_XRGB8888);
  ```
  Change `WL_SHM_FORMAT_XRGB8888` to `WL_SHM_FORMAT_ARGB8888`:
  ```c
      w->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)w->width,
                                            (int32_t)w->height, (int32_t)stride,
                                            WL_SHM_FORMAT_ARGB8888);
  ```

- [ ] In the same file, find the opaque-region block in `sb_wl_open()` (currently lines 289-296):
  ```c
      /* The bar strip is opaque -- tell the compositor so it can skip
       * blending what is underneath it. */
      struct wl_region *opaque = wl_compositor_create_region(w->compositor);
      if (opaque != NULL) {
          wl_region_add(opaque, 0, 0, (int32_t)w->width, (int32_t)w->height);
          wl_surface_set_opaque_region(w->surface, opaque);
          wl_region_destroy(opaque);
      }

      return w;
  ```
  Delete the whole block (comment, `struct wl_region *opaque` declaration, the `if` block, and the blank line after it), leaving just:
  ```c
      return w;
  ```
  (The bar is no longer opaque — leaving this hint in place would tell labwc it can skip blending the surface, defeating the translucency this whole task exists to add.)

### Step 7: Update `test_sb_render.c` — fix broken background-equality checks and add an opacity check

- [ ] Open `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`. This file currently compares against `QM_COLOR_BG` (alpha=0, fully opaque by the old convention) in four places, all of which will now be WRONG since `sb_render()` writes `QM_ARGB(204, 0, 0, 0)` (alpha=204) for its background, not `QM_COLOR_BG` (alpha=0):
  1. The `rightmost_nonbg_x()` helper (currently lines 46-54) compares `qm_get_pixel(fb, x, y) != QM_COLOR_BG` to find non-background pixels — after this change, EVERY pixel in the framebuffer (including the actual background) has alpha=204, so none of them equal the old `QM_COLOR_BG` (alpha=0) anymore, and this helper would incorrectly treat the entire background as "non-background."
  2. Three `qm_get_pixel(fb, 0, 0) == QM_COLOR_BG` checks (currently lines 63, 91, 93) will fail for the same reason.

  Fix: add a new constant right after the `#include` block, matching what `sb_render.c` actually writes:
  ```c
  /* Matches sb_render.c's background fill exactly (translucent black,
   * alpha=204 ~= 80% opacity) -- QM_COLOR_BG (alpha=0) no longer matches
   * what this file's background pixels actually contain. */
  #define SB_EXPECTED_BG QM_ARGB(204, 0, 0, 0)
  ```
  Then replace every one of the four `QM_COLOR_BG` references described above with `SB_EXPECTED_BG`:
  - In `rightmost_nonbg_x()`: `if (qm_get_pixel(fb, (int)x, (int)y) != QM_COLOR_BG)` becomes `if (qm_get_pixel(fb, (int)x, (int)y) != SB_EXPECTED_BG)`.
  - The three `check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, ...)` lines become `check(qm_get_pixel(fb, 0, 0) == SB_EXPECTED_BG, ...)`.

- [ ] Add a new test block for the alpha value itself, after the existing `"sb_render icon groups stay within screen width"` block (i.e. as the new last block, right before the final `printf("\n%s (%d failure%s)\n", ...)` summary line):
  ```c
      printf("sb_render background is translucent (alpha=204, ~80%% opacity)\n");
      {
          qm_fb *fb = make_fb(640, 40);
          sb_render(fb, 1, 50, 70, 87, 0);
          uint32_t bg_pixel = qm_get_pixel(fb, 0, 0);
          check(((bg_pixel >> 24) & 0xFF) == 204,
                "background alpha byte is 204 (80%% opacity)");
          free_fb(fb);
      }
  ```

### Step 8: Run the full host test suite

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: all four binaries (`test_qm_font`, `test_qm_icons`, `test_qm_settings`, `test_sb_render`) compile and print `PASSED (0 failures)`.

### Step 9: Incremental Buildroot compile-check for both packages

- [ ] Run:
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -30
  '
  ```
  Expected: no errors, ending in the generated `circuitsword-quickmenu` binary. (`circuitsword-quickmenu` itself only changed via the shared `qm_icons.c`/`quickmenu.h` — this check confirms the `QM_ARGB` addition and the `qm_draw_icon_rgba` change don't break anything on the still-`XRGB8888` quickmenu side.)

- [ ] Run the same pattern for the status bar package:
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-statusbar-rebuild 2>&1 | tail -30
  '
  ```
  Expected: no errors, ending in the generated `circuitsword-statusbar` binary.

### Step 10: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
          package/batocera/utils/circuitsword-quickmenu/qm_icons.c \
          package/batocera/utils/circuitsword-statusbar/sb_render.c \
          package/batocera/utils/circuitsword-statusbar/sb_wl.c
  git commit -m "circuitsword-statusbar: translucent background (80%), icons stay fully opaque"
  ```

### Step 11: Regenerate the patch capture

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- [ ] Verify the new code is present:
  ```bash
  grep -c "QM_ARGB" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "WL_SHM_FORMAT_ARGB8888" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
  Expected: both counts greater than 0.

### Step 12: Write the findings log

- [ ] Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md`:

```markdown
# Translucent Status Bar: Findings

Implements `docs/superpowers/specs/2026-08-10-translucent-statusbar-design.md`.
Makes `circuitsword-statusbar`'s background uniformly 80% opaque instead
of fully opaque black, so the running game stays partially visible
through the bar strip. Icons (battery/WiFi/volume/brightness) stay fully
opaque and legible on top of it.

## What was built

- `quickmenu.h`: new `QM_ARGB(a, r, g, b)` macro alongside the existing
  alpha-less `QM_RGB`.
- `qm_icons.c`'s `qm_draw_icon_rgba()`: every pixel it draws now
  explicitly carries alpha=255 (`QM_ARGB(255, ...)` instead of
  `QM_RGB(...)`) -- a shared-primitive change, harmless for
  `circuitsword-quickmenu` (still `XRGB8888`, alpha byte ignored), and
  what keeps icons fully opaque against the status bar's now-translucent
  background.
- `sb_render.c`: background fill changed from opaque `QM_COLOR_BG` to
  `QM_ARGB(204, 0, 0, 0)` (204/255 ~= 80% opacity).
- `sb_wl.c`: `wl_shm` buffer format changed from `WL_SHM_FORMAT_XRGB8888`
  to `WL_SHM_FORMAT_ARGB8888`; the `wl_surface_set_opaque_region(...)`
  hint (which told labwc it could skip blending this surface) removed
  entirely, since the surface is no longer opaque.
- `circuitsword-quickmenu`'s own `qm_wl.c` is untouched -- stays
  `XRGB8888`, stays fully opaque, by original design.

## Verified off-device

- `tests/test_qm_icons.c`: new check confirming every pixel
  `qm_draw_icon_rgba()` draws carries alpha=255, regardless of the input
  draw color's own (irrelevant, always-0) alpha bits.
- `tests/test_sb_render.c`: updated background-equality checks (and the
  `rightmost_nonbg_x()` helper) to compare against the actual new
  translucent background value instead of the old opaque `QM_COLOR_BG`;
  new check confirming the background's alpha byte is exactly 204.
- Incremental Buildroot compiles (`PKG=circuitsword-quickmenu-rebuild`
  and `PKG=circuitsword-statusbar-rebuild`) both succeed.
- Patch capture confirmed to contain the new code via targeted `grep`.

## Needs on-device validation (not yet done)

- **Whether labwc actually honors `ARGB8888` translucency for a
  `wlr-layer-shell` surface.** Every prior overlay surface on this
  hardware (`circuitsword-quickmenu`'s full-screen menu, and the status
  bar's own previous opaque incarnation) has been fully opaque -- this is
  the first translucent layer-shell surface attempted on this hardware/
  compositor combination. The mechanism (`ARGB8888` buffer + no opaque
  region hint) is the standard Wayland approach and should work, but this
  is genuinely new ground for this project and must be confirmed on the
  real device, not assumed.
- **Whether 80% is a good balance** between "status info stays legible"
  and "game stays visible" at actual DPI panel brightness/contrast --
  chosen as a reasonable starting value, not tuned against real hardware.
- **Any compositing artifacts** specific to alpha-blended layer-shell
  surfaces (flicker, incorrect blend order, edge artifacts) that a
  fully-opaque surface wouldn't have shown.
```

---
