#!/bin/sh
# build_mocks.sh -> bin/: the Wine rig's stand-ins, cross-built with MinGW.
# (On Windows, build_windows.ps1 builds the same programs with MSVC.)
set -e
h=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$h/../.." && pwd)
bin=${BIN:-$h/bin}
mkdir -p "$bin"
cxx="x86_64-w64-mingw32-g++ -std=c++17 -O2 -static"
# The fake NVIDIA driver, and the import library the app links against.
$cxx -shared -I"$h/nvwrap" -I"$repo/nvapi" "$h/mock/nvapi_mock.cpp" -o "$bin/nvapi64.dll" -Wl,--out-implib,"$bin/libnvapi64.a"
for p in exit3 fake_tar fg filelock holder winlist; do
  $cxx -municode "$h/mock/$p.cpp" -o "$bin/$p.exe" -luser32 2>/dev/null || $cxx "$h/mock/$p.cpp" -o "$bin/$p.exe" -luser32
done
for p in dummy_game ready_then_exit; do
  $cxx -municode -mwindows "$h/mock/$p.cpp" -o "$bin/$p.exe" -luser32 -lshlwapi
done
cp "$bin/dummy_game.exe" "$bin/gameA.exe"; cp "$bin/dummy_game.exe" "$bin/gameB.exe"
echo "built mocks into $bin"
