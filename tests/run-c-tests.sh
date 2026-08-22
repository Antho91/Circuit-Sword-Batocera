#!/bin/bash
# Host-side unit tests for circuitsword-quickmenu and circuitsword-statusbar's
# pure (non-Wayland, non-evdev) code. Compiles straight out of the real build
# tree with the host compiler -- no cross toolchain, no Wayland, no device.
set -euo pipefail

BASE="${BATOCERA_SRC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/batocera-build/build/batocera.linux}/package/batocera/utils"
QM_SRC="$BASE/circuitsword-quickmenu"
SB_SRC="$BASE/circuitsword-statusbar"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# quickmenu.c now calls qm_draw_text_ttf()/qm_ttf_text_width() unconditionally
# (Task 2 of the overlay-freetype-text plan switched every call site over),
# so any test binary that links quickmenu.c must also link qm_ttf.c -- which
# in turn hard-requires FreeType headers/libs to even compile (matches the
# real Buildroot package: circuitsword-quickmenu now DEPENDS on freetype
# unconditionally, see circuitsword-quickmenu.mk). Probe for FreeType here,
# ahead of the test_qm_font build, instead of down by test_qm_ttf, since
# test_qm_font needs it too now.
QM_TTF_CFLAGS=""
QM_TTF_LIBS=""
HAVE_FREETYPE=0
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists freetype2 2>/dev/null; then
    QM_TTF_CFLAGS="$(pkg-config --cflags freetype2)"
    QM_TTF_LIBS="$(pkg-config --libs freetype2)"
    HAVE_FREETYPE=1
elif [ -f /opt/homebrew/opt/freetype/include/freetype2/ft2build.h ]; then
    QM_TTF_CFLAGS="-I/opt/homebrew/opt/freetype/include/freetype2"
    QM_TTF_LIBS="-L/opt/homebrew/opt/freetype/lib -lfreetype"
    HAVE_FREETYPE=1
elif [ -f /usr/local/opt/freetype/include/freetype2/ft2build.h ]; then
    QM_TTF_CFLAGS="-I/usr/local/opt/freetype/include/freetype2"
    QM_TTF_LIBS="-L/usr/local/opt/freetype/lib -lfreetype"
    HAVE_FREETYPE=1
elif [ -f /usr/include/freetype2/ft2build.h ]; then
    QM_TTF_CFLAGS="-I/usr/include/freetype2"
    QM_TTF_LIBS="-lfreetype"
    HAVE_FREETYPE=1
fi

if [ "$HAVE_FREETYPE" = "1" ]; then
    cc -std=gnu99 -O1 -Wall -Wextra -Werror \
       -DQM_NO_MAIN \
       -I"$QM_SRC" $QM_TTF_CFLAGS \
       "$HERE/test_qm_font.c" "$QM_SRC/qm_font.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_icon_data.c" "$QM_SRC/quickmenu.c" "$QM_SRC/qm_ttf.c" \
       $QM_TTF_LIBS \
       -o "$OUT/test_qm_font"

    "$OUT/test_qm_font"
else
    echo "skipping test_qm_font (freetype2 headers not found on host -- quickmenu.c now hard-requires FreeType, matching the real Buildroot package dependency)"
fi

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -DQM_NO_MAIN \
   -I"$QM_SRC" \
   "$HERE/test_qm_icons.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_icon_data.c" "$QM_SRC/qm_font.c" \
   -o "$OUT/test_qm_icons"

"$OUT/test_qm_icons"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$QM_SRC" \
   "$HERE/test_qm_settings.c" "$QM_SRC/qm_settings.c" \
   -o "$OUT/test_qm_settings"

"$OUT/test_qm_settings"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$SB_SRC" -I"$QM_SRC" \
   "$HERE/test_sb_render.c" "$SB_SRC/sb_render.c" "$QM_SRC/qm_font.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_icon_data.c" \
   -o "$OUT/test_sb_render"

"$OUT/test_sb_render"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$QM_SRC" \
   "$HERE/test_qm_joystick.c" "$QM_SRC/qm_joystick.c" \
   -o "$OUT/test_qm_joystick"

"$OUT/test_qm_joystick"

# test_qm_ttf needs the same FreeType headers/libs probed above for
# test_qm_font -- reuse QM_TTF_CFLAGS/QM_TTF_LIBS/HAVE_FREETYPE. Skip
# cleanly (not a failure) if none found.
if [ "$HAVE_FREETYPE" = "1" ]; then
    cc -std=gnu99 -O1 -Wall -Wextra -Werror \
       -I"$QM_SRC" $QM_TTF_CFLAGS \
       "$HERE/test_qm_ttf.c" "$QM_SRC/qm_ttf.c" "$QM_SRC/qm_font.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_icon_data.c" \
       $QM_TTF_LIBS \
       -o "$OUT/test_qm_ttf"

    "$OUT/test_qm_ttf"
else
    echo "skipping test_qm_ttf (freetype2 headers not found on host)"
fi
