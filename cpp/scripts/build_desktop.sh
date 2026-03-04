#!/usr/bin/env bash
# build_desktop.sh — Build phigros_render for the current desktop platform.
# Usage: ./scripts/build_desktop.sh [Release|Debug] [extra cmake args...]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_TYPE="${1:-Release}"
shift 2>/dev/null || true

BUILD_DIR="$CPP_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[Desktop] Configuring (${BUILD_TYPE})…"
cmake "$CPP_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DUSE_BGFX=OFF \
    -DUSE_SDL3=ON \
    "$@"

echo "[Desktop] Building…"
cmake --build . --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "[Desktop] Done → $BUILD_DIR/phigros_render"
echo "Usage: $BUILD_DIR/phigros_render <chart> [options]"
echo "       $BUILD_DIR/phigros_render --help"
