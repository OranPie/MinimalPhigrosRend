#!/usr/bin/env bash
# build_web.sh — Build phigros_render for the browser via Emscripten.
#
# Prerequisites:
#   source /path/to/emsdk/emsdk_env.sh   (activates emcc in PATH)
#
# Output:
#   build_web/phigros_render.js
#   build_web/phigros_render.wasm
#   build_web/phigros_render.html        (standalone demo page)
#   build_web/phigros_render.data        (if respack.zip found)
#
# Usage:
#   ./scripts/build_web.sh [Release|Debug] [extra cmake args...]
#   Then: python3 -m http.server 8080 -d build_web/
#         open http://localhost:8080/phigros_render.html
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_TYPE="${1:-Release}"
shift 2>/dev/null || true

# Check emcc is available
if ! command -v emcc &>/dev/null; then
    echo "[Error] emcc not found. Activate Emscripten SDK first:"
    echo "  source /path/to/emsdk/emsdk_env.sh"
    exit 1
fi
echo "[Web] Using $(emcc --version | head -1)"

BUILD_DIR="$CPP_DIR/build_web"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[Web] Configuring (${BUILD_TYPE})…"
emcmake cmake "$CPP_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DUSE_BGFX=OFF \
    -DUSE_SDL3=OFF \
    "$@"

echo "[Web] Building…"
emmake cmake --build . --parallel "$(nproc 2>/dev/null || echo 4)" \
    --target phigros_render

echo ""
echo "[Web] Done → $BUILD_DIR/"
echo "  phigros_render.js"
echo "  phigros_render.wasm"
echo "  phigros_render.html"
echo ""
echo "Serve with:"
echo "  python3 -m http.server 8080 -d $BUILD_DIR"
echo "  open http://localhost:8080/phigros_render.html"
