# Visual Toggle Switches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace text-based on/off indicators in `circuitsword-quickmenu` (the WiFi row, the Joystick submenu's `[X]`/`[ ]` marks, and the Daemon Settings status row's fan text) with a real pill-shaped toggle-switch graphic, vendored from Batocera's own EmulationStation.

**Architecture:** Vendor `on.svg`/`off.svg` from `batocera-emulationstation` into the existing icon-baking pipeline (`tools/convert-icons.py` → `qm_icon_data.c`) as two new non-square (32x16) icons. Three call sites in `quickmenu.c`'s `qm_render()` swap a `qm_draw_text_ttf()` call for a `qm_draw_icon_rgba()` call using the new icons, right-aligned at a fixed width instead of the current variable-width text alignment.

**Tech Stack:** C (circuitsword-quickmenu), Python (tools/convert-icons.py, needs cairosvg+pillow), Buildroot/Docker build pipeline.

## Global Constraints

- Asset source: `batocera-emulationstation` at commit `2c29a330e487210a7d51ad2650bb7b280ea44c86` (MIT-licensed), files `resources/on.svg` and `resources/off.svg`.
- New icon size: 32x16 pixels (not the usual 24x24 `ICON_SIZE` square — matches the source SVGs' ~2:1 aspect ratio; `QM_ICON_TITLE` already proves non-square icons work with the existing pipeline).
- New enum names: `QM_ICON_TOGGLE_ON`, `QM_ICON_TOGGLE_OFF`, inserted into `qm_icon_kind` right after `QM_ICON_SETTINGS` and before `QM_ICON_TITLE`.
- Color: single runtime tint via the existing `qm_draw_icon_rgba(fb, x, y, kind, color)` signature — reuse `QM_COLOR_FG`/`QM_COLOR_DIM` exactly as each call site already does today. No new color constants.
- Scope: WiFi row (`QM_SCREEN_MAIN`), all 6 toggle rows in the Joystick submenu (`QM_SCREEN_JOYSTICK`), and the Daemon Settings status row's fan-state portion (`QM_SCREEN_DAEMON_SETTINGS`) — per the design doc's Scope section. The Daemon Settings `ds_message` error-text branch is unchanged (stays plain text).
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (detached HEAD). After each task's commit, regenerate the patch:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux && git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- Host tests live in `/Users/bas/Circuit-Sword Batocera/tests/` (no git repo there — edits are never committed, only made on disk). Run via `bash run-c-tests.sh` from that directory.

---

### Task 1: Vendor the toggle-switch SVGs and bake them into the icon pipeline

**Files:**
- Create: `package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-on.svg`
- Create: `package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-off.svg`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h` (add two enum members)
- Modify: `tools/convert-icons.py` (add two `ICONS` list entries)
- Regenerate: `package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c` (run the script, do not hand-edit)

**Interfaces:**
- Produces: `QM_ICON_TOGGLE_ON`, `QM_ICON_TOGGLE_OFF` — two new `qm_icon_kind` enum values, each a baked 32x16 RGBA icon drawable via the existing `qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color)`. Tasks 2, 3, and 4 consume these two names directly.

- [ ] **Step 1: Fetch and save the two source SVGs**

Run this exact command (uses the `gh` CLI, already authenticated in this environment) to fetch `on.svg`:

```bash
gh api "repos/batocera-linux/batocera-emulationstation/contents/resources/on.svg?ref=2c29a330e487210a7d51ad2650bb7b280ea44c86" --jq '.content' | base64 -d > /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-on.svg
```

And this for `off.svg`:

```bash
gh api "repos/batocera-linux/batocera-emulationstation/contents/resources/off.svg?ref=2c29a330e487210a7d51ad2650bb7b280ea44c86" --jq '.content' | base64 -d > /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-off.svg
```

Verify both files are non-empty and start with `<?xml version="1.0"`:

```bash
head -c 60 /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-on.svg
head -c 60 /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-off.svg
```

Expected: both print `<?xml version="1.0" encoding="UTF-8" standalone="no"?>` (or the start of a valid XML/SVG declaration). If either command fails (e.g. `gh` not authenticated, network error), stop and report — do not fabricate placeholder SVG content.

- [ ] **Step 2: Add the two enum members to `quickmenu.h`**

Read `package/batocera/utils/circuitsword-quickmenu/quickmenu.h` fresh first and find the `qm_icon_kind` enum (currently ends `... QM_ICON_JOYSTICK, QM_ICON_SETTINGS, QM_ICON_TITLE, QM_ICON_BADGE_A, ...`). Insert the two new members between `QM_ICON_SETTINGS` and `QM_ICON_TITLE`:

```c
    QM_ICON_JOYSTICK,
    QM_ICON_SETTINGS,
    QM_ICON_TOGGLE_ON,
    QM_ICON_TOGGLE_OFF,
    QM_ICON_TITLE,
```

Also add two size constants near the existing `#define QM_ICON_SIZE 24` line:

```c
#define QM_TOGGLE_ICON_W 32   /* width of the toggle-switch icon (non-square, unlike QM_ICON_SIZE) */
#define QM_TOGGLE_ICON_H 16   /* height of the toggle-switch icon */
```

- [ ] **Step 3: Add the two `ICONS` list entries in `tools/convert-icons.py`**

Read `/Users/bas/batocera-build-wifi/batocera.linux/tools/convert-icons.py` fresh first. Find the `ICONS` list (currently ends `... ("QM_ICON_JOYSTICK", "joystick.svg", ICON_SIZE, ICON_SIZE), ("QM_ICON_SETTINGS", "settings.svg", ICON_SIZE, ICON_SIZE), ("QM_ICON_TITLE", "title.svg", TITLE_W, TITLE_H), ...`). Insert two entries between the `QM_ICON_SETTINGS` line and the `QM_ICON_TITLE` line:

```python
    ("QM_ICON_SETTINGS", "settings.svg", ICON_SIZE, ICON_SIZE),
    ("QM_ICON_TOGGLE_ON", "toggle-on.svg", 32, 16),
    ("QM_ICON_TOGGLE_OFF", "toggle-off.svg", 32, 16),
    ("QM_ICON_TITLE", "title.svg", TITLE_W, TITLE_H),
```

- [ ] **Step 4: Regenerate `qm_icon_data.c`**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 -m pip install --quiet cairosvg pillow 2>/dev/null || pip3 install --quiet cairosvg pillow
python3 tools/convert-icons.py
```

Confirm the generated file changed and contains both new entries:

```bash
grep -c "QM_ICON_TOGGLE_ON\|QM_ICON_TOGGLE_OFF" package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c
```

Expected: at least 2 (one array-index/comment reference per icon; exact count depends on the generator's output format, but must be greater than 0).

- [ ] **Step 5: Guard against a silent rasterization failure**

The 32x16 non-square size is new for this pipeline (only `QM_ICON_TITLE` has used a non-square size before). Write a one-off Python check to confirm neither new icon's baked alpha data is all-zero:

```bash
python3 -c "
import re
src = open('package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c').read()
for name in ('QM_ICON_TOGGLE_ON', 'QM_ICON_TOGGLE_OFF'):
    # Find the array literal following this icon's definition block.
    idx = src.index(name)
    chunk = src[idx:idx+4000]
    nums = re.findall(r'-?\d+', chunk)
    nonzero = sum(1 for n in nums if n not in ('0',))
    print(name, 'nonzero-count-in-nearby-data:', nonzero)
    assert nonzero > 0, f'{name} appears to have no non-zero data nearby -- possible blank rasterization'
print('OK: both icons have non-zero baked data')
"
```

Expected output ends with `OK: both icons have non-zero baked data`. If the assertion fails, the SVG likely failed to rasterize at this size — do not proceed; investigate the CairoSVG output directly (e.g. render `toggle-on.svg` to a PNG manually and inspect it) before continuing.

- [ ] **Step 6: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
bash run-c-tests.sh
```

Expected: `test_qm_icons.c`'s "every icon draws at least one non-background pixel" loop (which iterates `QM_ICON_COUNT`) now covers both new icons automatically and reports 0 failures across the whole suite. `quickmenu.c` does not yet reference the new icons (that's Tasks 2-4), so no other test behavior changes in this task.

- [ ] **Step 7: Rebuild the `circuitsword-quickmenu` package and verify in the Docker volume**

Per this project's Hard Rule #7 (a full image build silently reuses already-built packages), force a package-level rebuild rather than relying on a full image build to pick up the change:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

Verify the new icon data actually landed in the built binary:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c QM_ICON_TOGGLE" 2>/dev/null || true
```

(This may print 0 even on success — enum names are not necessarily embedded as strings in a compiled binary. The authoritative check is the build completing with no errors and the alpha-data check from Step 5 having passed against source before compiling. Note either outcome in the report; do not treat a 0 here as a failure on its own.)

- [ ] **Step 8: Commit and regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-on.svg \
        package/batocera/utils/circuitsword-quickmenu/icons/src/toggle-off.svg \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c \
        ../../tools/convert-icons.py 2>/dev/null
git add tools/convert-icons.py
git commit -m "circuitsword-quickmenu: vendor and bake Batocera's on/off switch icons"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

(If the first `git add` for `tools/convert-icons.py` fails because the relative path from `package/batocera/utils/circuitsword-quickmenu` is wrong, use the absolute path `/Users/bas/batocera-build-wifi/batocera.linux/tools/convert-icons.py` instead — confirm the correct add succeeded via `git status` before committing.)

---

### Task 2: Replace the WiFi row's text indicator on the main screen

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (the `QM_ITEM_WIFI` branch inside `qm_render()`'s `QM_SCREEN_MAIN` section)

**Interfaces:**
- Consumes: `QM_ICON_TOGGLE_ON`, `QM_ICON_TOGGLE_OFF`, `QM_TOGGLE_ICON_W`, `QM_TOGGLE_ICON_H` (Task 1). `qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color)` (existing).

- [ ] **Step 1: Read the current WiFi-row code**

Read `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` and find the `if (item == QM_ITEM_WIFI) { ... }` block inside `qm_render()`'s `QM_SCREEN_MAIN` branch (currently around line 101-106). Confirm the exact current code matches this (it may have shifted slightly if earlier tasks touched nearby lines — match by content, not line number):

```c
        if (item == QM_ITEM_WIFI) {
            uint32_t wifi_fg = st->wifi_on ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_WIFI, wifi_fg);
            snprintf(label, sizeof(label), "WiFi: %s", st->wifi_on ? "ON" : "OFF");
            int wifi_label_x = (int)fb->width - margin - qm_ttf_text_width(label, px_size);
            qm_draw_text_ttf(fb, wifi_label_x, label_y, label, px_size, wifi_fg);
        } else if (item == QM_ITEM_VOLUME) {
```

If the code differs from this (e.g. different variable names), use what's actually there — the shape of the change (replace the `snprintf`+`qm_draw_text_ttf` pair with a single `qm_draw_icon_rgba` call, right-aligned at a fixed width) is what matters, not exact variable names.

- [ ] **Step 2: Replace the text indicator with the icon**

```c
        if (item == QM_ITEM_WIFI) {
            uint32_t wifi_fg = st->wifi_on ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_WIFI, wifi_fg);
            int wifi_icon_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
            int wifi_icon_y = y + (QM_ICON_SIZE - QM_TOGGLE_ICON_H) / 2;
            qm_draw_icon_rgba(fb, wifi_icon_x, wifi_icon_y,
                               st->wifi_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF,
                               wifi_fg);
        } else if (item == QM_ITEM_VOLUME) {
```

(`label_y` and the `snprintf`-filled `label` buffer are still used by the `QM_ITEM_VOLUME`/`QM_ITEM_BRIGHTNESS` branches below — do not remove their declarations, only stop using them inside the `QM_ITEM_WIFI` branch.)

- [ ] **Step 3: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
bash run-c-tests.sh
```

Expected: 0 failures. `test_qm_font.c`'s `QM_SCREEN_MAIN` render tests (at both 320x240 and 640x480) only assert that the rightmost drawn pixel stays within the panel width — they contain no hardcoded expected pixel position for the WiFi row, so they should pass unmodified as long as the new icon's fixed-32px right-aligned position stays in bounds (it does: `margin` is a few pixels, `QM_TOGGLE_ICON_W` is 32, well under both 320px and 640px panel widths).

- [ ] **Step 4: Rebuild and verify**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

Confirm no build errors in the output.

- [ ] **Step 5: Commit and regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: replace WiFi row's text ON/OFF with the toggle-switch icon"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 3: Replace the Joystick submenu's `[X]`/`[ ]` marks

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (the `status_bit[item] >= 0` block inside `qm_render()`'s `QM_SCREEN_JOYSTICK` branch)

**Interfaces:**
- Consumes: `QM_ICON_TOGGLE_ON`, `QM_ICON_TOGGLE_OFF`, `QM_TOGGLE_ICON_W`, `QM_TOGGLE_ICON_H` (Task 1).

- [ ] **Step 1: Read the current Joystick submenu code**

Read `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` and find the row-drawing loop inside the `QM_SCREEN_JOYSTICK` branch. Confirm it currently looks like this (match by content, not line number, since earlier tasks may have shifted lines):

```c
        int row_h2 = QM_GLYPH_H * scale + 2 * scale;
        int y2 = margin;
        for (int item = 0; item < QM_JOY_COUNT; item++) {
            if (item == st->joy_selected)
                qm_fill_rect(fb, margin / 2, y2 - scale,
                             (int)fb->width - margin, row_h2, QM_COLOR_SEL_BG);
            uint32_t fg2 = (item == st->joy_selected) ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y2, labels[item], px_size, fg2);
            if (status_bit[item] >= 0) {
                const char *mark = (st->joy_status[status_bit[item]] == '1') ? "[X]" : "[ ]";
                int mark_x = (int)fb->width - margin - qm_ttf_text_width(mark, px_size);
                qm_draw_text_ttf(fb, mark_x, y2, mark, px_size, fg2);
            }
            y2 += row_h2;
        }
```

- [ ] **Step 2: Replace the text mark with the icon**

```c
        int row_h2 = QM_GLYPH_H * scale + 2 * scale;
        int y2 = margin;
        for (int item = 0; item < QM_JOY_COUNT; item++) {
            if (item == st->joy_selected)
                qm_fill_rect(fb, margin / 2, y2 - scale,
                             (int)fb->width - margin, row_h2, QM_COLOR_SEL_BG);
            uint32_t fg2 = (item == st->joy_selected) ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y2, labels[item], px_size, fg2);
            if (status_bit[item] >= 0) {
                qm_icon_kind mark_icon = (st->joy_status[status_bit[item]] == '1')
                                        ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
                int mark_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
                int mark_y = y2 + (px_size - QM_TOGGLE_ICON_H) / 2;
                qm_draw_icon_rgba(fb, mark_x, mark_y, mark_icon, fg2);
            }
            y2 += row_h2;
        }
```

The `status_bit[]` array itself (mapping row index to bit index in `st->joy_status[6]`) is unchanged — only the drawn indicator changes from text to icon.

- [ ] **Step 3: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
bash run-c-tests.sh
```

Expected: 0 failures. `test_qm_font.c`'s `QM_SCREEN_JOYSTICK` render tests assert only that the rightmost pixel stays within panel bounds — no hardcoded position to update.

- [ ] **Step 4: Rebuild and verify**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

- [ ] **Step 5: Commit and regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: replace Joystick submenu's [X]/[ ] marks with the toggle-switch icon"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 4: Replace the Daemon Settings status row's fan ON/OFF text

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (the status-row `snprintf` calls and the render loop's status-row handling, inside `qm_render()`'s `QM_SCREEN_DAEMON_SETTINGS` branch)

**Interfaces:**
- Consumes: `QM_ICON_TOGGLE_ON`, `QM_ICON_TOGGLE_OFF`, `QM_TOGGLE_ICON_W`, `QM_TOGGLE_ICON_H` (Task 1).

- [ ] **Step 1: Read the current Daemon Settings status-row code**

Read `package/batocera/utils/circuitsword-quickmenu/quickmenu.c` and find the `QM_SCREEN_DAEMON_SETTINGS` branch. Confirm it currently looks like this (match by content, not line number):

```c
        char labels[QM_DS_COUNT][32];
        char values[QM_DS_COUNT][16];
        snprintf(labels[QM_DS_FAN_ON_TEMP], sizeof(labels[0]), "Fan ON temp:");
        snprintf(values[QM_DS_FAN_ON_TEMP], sizeof(values[0]),
                 "%.1fC", st->ds_fan_on_temp);
        snprintf(labels[QM_DS_FAN_OFF_TEMP], sizeof(labels[0]), "Fan OFF temp:");
        snprintf(values[QM_DS_FAN_OFF_TEMP], sizeof(values[0]),
                 "%.1fC", st->ds_fan_off_temp);
        snprintf(labels[QM_DS_POLL_INTERVAL], sizeof(labels[0]), "Fan poll interval:");
        snprintf(values[QM_DS_POLL_INTERVAL], sizeof(values[0]),
                 "%ds", st->ds_fan_poll_interval_s);
        snprintf(labels[QM_DS_DEBOUNCE_MS], sizeof(labels[0]), "Switch debounce:");
        snprintf(values[QM_DS_DEBOUNCE_MS], sizeof(values[0]),
                 "%dms", st->ds_switch_debounce_ms);
        if (st->ds_message[0] != '\0') {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]), "%s", st->ds_message);
        } else if (st->ds_cpu_temp_ok) {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]),
                     "CPU: %.1fC, Fan: %s", st->ds_cpu_temp, st->ds_fan_on ? "ON" : "OFF");
        } else {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]),
                     "Fan is currently: %s", st->ds_fan_on ? "ON" : "OFF");
        }

        int y3 = margin;
        for (int item = 0; item < QM_DS_COUNT; item++) {
            if (item == QM_DS_STATUS_ROW) {
                /* Visual gap before the read-only status row. */
                y3 += row_h3 / 2;
            }
            if (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                qm_fill_rect(fb, margin / 2, y3 - scale,
                             (int)fb->width - margin, row_h3, QM_COLOR_SEL_BG);
            uint32_t fg3 = (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                           ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y3, labels[item], px_size, fg3);
            if (item != QM_DS_STATUS_ROW) {
                int value_x = (int)fb->width - margin
                            - qm_ttf_text_width(values[item], px_size);
                qm_draw_text_ttf(fb, value_x, y3, values[item], px_size, fg3);
            }
            y3 += row_h3;
        }
```

- [ ] **Step 2: Split the status row into text-prefix + icon**

Replace the status-row `snprintf` block (the `if (st->ds_message[0] != '\0') { ... } else if (...) { ... } else { ... }` block) with a version that drops the literal "ON"/"OFF" word from the two non-message branches, and add an `int ds_status_icon` variable to carry which icon (if any) to draw:

```c
        int ds_status_icon = -1;  /* -1 = no icon (a message is shown instead) */
        if (st->ds_message[0] != '\0') {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]), "%s", st->ds_message);
        } else if (st->ds_cpu_temp_ok) {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]),
                     "CPU: %.1fC, Fan:", st->ds_cpu_temp);
            ds_status_icon = st->ds_fan_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
        } else {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]), "Fan is currently:");
            ds_status_icon = st->ds_fan_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
        }
```

Then update the render loop to draw the icon for the status row when `ds_status_icon >= 0`:

```c
        int y3 = margin;
        for (int item = 0; item < QM_DS_COUNT; item++) {
            if (item == QM_DS_STATUS_ROW) {
                /* Visual gap before the read-only status row. */
                y3 += row_h3 / 2;
            }
            if (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                qm_fill_rect(fb, margin / 2, y3 - scale,
                             (int)fb->width - margin, row_h3, QM_COLOR_SEL_BG);
            uint32_t fg3 = (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                           ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y3, labels[item], px_size, fg3);
            if (item != QM_DS_STATUS_ROW) {
                int value_x = (int)fb->width - margin
                            - qm_ttf_text_width(values[item], px_size);
                qm_draw_text_ttf(fb, value_x, y3, values[item], px_size, fg3);
            } else if (ds_status_icon >= 0) {
                int icon_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
                int icon_y = y3 + (px_size - QM_TOGGLE_ICON_H) / 2;
                qm_draw_icon_rgba(fb, icon_x, icon_y, (qm_icon_kind)ds_status_icon, fg3);
            }
            y3 += row_h3;
        }
```

This row stays read-only/non-interactive — no input-handling code changes anywhere in this task.

- [ ] **Step 3: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
bash run-c-tests.sh
```

Expected: 0 failures. `test_qm_font.c`'s Daemon Settings render tests (including the CPU-temp-ok variant and the message-branch variant, at both panel sizes) assert only that the rightmost pixel stays within panel bounds and that something non-background renders — no hardcoded position, so they pass unmodified.

- [ ] **Step 4: Rebuild and verify**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

- [ ] **Step 5: Commit and regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: replace Daemon Settings fan ON/OFF text with the toggle-switch icon"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

## Self-Review Notes

**Spec coverage:** every "Applies everywhere" bullet from the design doc's Scope section has a task — WiFi row (Task 2), all 6 Joystick toggle rows (Task 3), Daemon Settings status row (Task 4), asset vendoring + baking (Task 1). The `ds_message` branch staying text-only is explicitly preserved in Task 4's Step 2 (the `if` branch is untouched). Color treatment (single tint, no new accent) is satisfied by every call site reusing the existing `wifi_fg`/`fg2`/`fg3` variables already computed for selected/unselected state — no new color logic anywhere in this plan.

**Type consistency:** `QM_ICON_TOGGLE_ON`/`QM_ICON_TOGGLE_OFF` (Task 1) are used identically as the third argument to `qm_draw_icon_rgba()` in Tasks 2, 3, and 4. `QM_TOGGLE_ICON_W`/`QM_TOGGLE_ICON_H` (Task 1) are used identically for right-alignment math in all three consuming tasks.

**Note on test files:** unlike most of this project's earlier plans, none of the `tests/test_qm_font.c` assertions need editing — they were already written as generic "stays within panel bounds" checks rather than exact-pixel checks, so they exercise the new code automatically once `quickmenu.c` changes. Each task's Step 3 re-runs the existing suite as verification rather than modifying test content.
