#!/usr/bin/env bash
# Host test of the Lua engine (Linux/macOS/WSL): builds the engine + Lua 5.1 with a fake Pokémon Platinum (ITA)
# memory image and runs a script for a few frames, writing the overlay to a PPM image.
# Usage: ./build_and_run.sh <path to melonDS-android checkout> <script.lua> [frames] [key@frame ...]
set -euo pipefail
REPO="$1"; SCRIPT="$2"; FRAMES="${3:-5}"; shift 3 || true
CPP="$REPO/app/src/main/cpp"
HERE="$(cd "$(dirname "$0")" && pwd)"
${CXX:-c++} -std=c++17 -O1 -g -DLUA_USE_POSIX -w -I"$CPP" "$HERE/test_main.cpp" \
    "$CPP/lua/LuaCanvas.cpp" "$CPP/lua/LuaEngine.cpp" "$CPP"/lua51/*.cpp -o "$HERE/lua_host_test"
cd "$HERE"
./lua_host_test "$SCRIPT" overlay.ppm "$FRAMES" "$@"
echo "Overlay written to $HERE/overlay.ppm"
