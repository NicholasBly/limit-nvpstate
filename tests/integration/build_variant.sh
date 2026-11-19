#!/bin/sh
# build_variant.sh <name> <source dir>  ->  out/<name>/limit-nvpstate.exe (Wine rig; MinGW + static Qt)
# QT5WIN: the cross-built static Qt (default /home/claude/qt5win).
set -e
name=$1; src=$(cd "$2" && pwd)
h=$(cd "$(dirname "$0")" && pwd)
qt=${QT5WIN:-/home/claude/qt5win}
b=$h/build/$name; o=$h/out/$name
rm -rf "$b"; mkdir -p "$b" "$o"
cd "$b"
"$qt/bin/qmake" VARIANT_SRC="$src" ${APPVER:+APPVER=$APPVER} "$h/variant.pro" > qmake.log 2>&1
make -j2 > make.log 2>&1 || { grep -E "error" make.log | head -20; exit 1; }
cp $(ls release/limit-nvpstate.exe limit-nvpstate.exe 2>/dev/null | head -1) "$o/"
cp "$h/bin/nvapi64.dll" "$o/"
# The updater helper, if this source tree has one (plain Win32, no Qt).
if [ -f "$src/../updater-helper/main.cpp" ]; then
  x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static \
    -I"$src" -I"$h/include/casefix" -DLIMITNV_TEST_HOOKS -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
    "$src/../updater-helper/main.cpp" "$src/updatetxn.cpp" "$src/updaterules.cpp" -lshell32 \
    -o "$o/limit-nvpstate-updater.exe" > "$b/helper.log" 2>&1 || { cat "$b/helper.log" | head -20; exit 1; }
  grep -E "warning" "$b/helper.log" | head -5
fi
echo "built $name"
