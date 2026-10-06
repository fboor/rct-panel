#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: MIT
#
# Build and run the simulator.
#
#   run.sh                 German build, 480 x 480, window
#   run.sh --lang en       English build
#   run.sh --shot out.png  one frame to a file, then exit
#
# LVGL is compiled once into build/lvgl and reused; a rebuild of the simulator's
# own three files then takes seconds. The tree comes out of the PlatformIO
# library directory, so the simulator runs against the same LVGL 9.3.0 the panel
# builds against - set LVGL_DIR if the library is somewhere else.
#
# bash, not sh: the object-file names are rewritten with a substitution that
# dash does not have.
set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
build="$here/build"
lvgl=${LVGL_DIR:-$repo/.pio/libdeps/esp32-s3/lvgl}
out="$build/panel_sim"

if [ ! -d "$lvgl/src" ]; then
    echo "LVGL nicht gefunden: $lvgl/src" >&2
    echo "LVGL_DIR setzen oder PlatformIO einmal laufen lassen." >&2
    exit 1
fi

mkdir -p "$build/lvgl"

# The firmware's own configuration file, plus the SDL driver. Same 16-bit colour
# and same 64 kB heap as the panel, so a picture means what it means there.
# PANEL_SIM: the firmware's own uiLayout() steps aside and the simulator
# supplies the profile it was asked for. That is the plan's compile-time choice
# working, not a special case.
cflags="-O2 -DPANEL_SIM -DLV_CONF_INCLUDE_SIMPLE -I$here/lvconf -I$repo/include -I$repo/src -I$lvgl -I$here -I$here/stubs"
ldflags="-lSDL2 -lz -lm -lpthread"

# The language is a build flag, so run.sh scans for it and builds accordingly -
# and does NOT shift it out of "$@", because the program is started with the same
# arguments at the end and a shift here would eat the first one.
lang=""
for a in "$@"; do
    case "$a" in
        --lang|--lang=en) lang="-DRCT_LANG_EN=1" ;;
    esac
done

# --- LVGL, once ---------------------------------------------------------------
# The object files are keyed by their own path, so adding or removing a source
# does not rebuild the world.
neu=0
while IFS= read -r f; do
    rel=${f#"$lvgl/src/"}
    o="$build/lvgl/${rel//\//_}.o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        cc $cflags -c "$f" -o "$o" &
        neu=$((neu + 1))
        if [ $((neu % 32)) -eq 0 ]; then wait; fi
    fi
done <<EOF
$(find "$lvgl/src" -name '*.c')
EOF
wait
[ $neu -gt 0 ] && echo "LVGL: $neu Dateien gebaut"

# --- the shipped C sources, once ---------------------------------------------
# The fonts and the language tables are firmware sources, not simulator code, so
# they are compiled here rather than faked.
#
# The fonts with cc and not with c++, and that is not a matter of taste: g++ takes
# a .c file as C++, and a namespace-scope `const` has INTERNAL linkage in C++ and
# external in C. Compiled as C++ the font objects lose their names and the link
# fails with "undefined reference to lv_font_montserrat_16_uml" - which looks like
# a missing file and is not.
mkdir -p "$build/schiff"
for f in "$repo"/src/gui/fonts/*.c; do
    o="$build/schiff/$(basename "${f%.c}").o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        cc $cflags -c "$f" -o "$o"
    fi
done

# --- the simulator ------------------------------------------------------------
# LVGL, the panel's GUI and the three simulator files. GuiApp.cpp is the shipped
# one, not a copy: that is the entire claim of this program.
#
# c++ and not cc: the simulator's own files are C++, and linking them with the C
# driver leaves every std::string method undefined at the link step rather than at
# the compile step, which is a much worse place to find out.
c++ $cflags $lang \
    "$here/sim_main.cpp" \
    "$here/sim_stubs.cpp" \
    "$here/sim_data.cpp" \
    "$here/sim_device.cpp" \
    "$repo/src/device/Device.cpp" \
    "$repo/src/device/DeviceFactory.cpp" \
    "$repo/src/rct/RctDriver.cpp" \
    "$repo/src/oig/OigDriver.cpp" \
    "$repo/src/web/WebServer.cpp" \
    "$repo/src/gui/GuiApp.cpp" \
    "$here/sim_board.cpp" \
    "$repo/src/ui/UiLayout.cpp" \
    "$repo"/src/i18n/Lang*.cpp \
    "$build"/schiff/*.o \
    "$build"/lvgl/*.o \
    -o "$out" $ldflags

echo "gebaut: $out"
exec "$out" "$@"