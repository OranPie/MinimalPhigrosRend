#!/usr/bin/env bash
# build_web.sh — Build the SDL app for the browser via Emscripten.
#
# Prerequisites:
#   source /path/to/emsdk/emsdk_env.sh   (activates emcc in PATH)
#
# Output:
#   build_web/phigros_sdl_app.js
#   build_web/phigros_sdl_app.wasm
#   build_web/phigros_sdl_app.html        (standalone demo page)
#   build_web/phigros_sdl_app.data        (if respack.zip found)
#
# Usage:
#   ./scripts/build_web.sh [Release|Debug] [extra cmake args...]
#   Then: python3 -m http.server 8080 -d build_web/
#         open http://localhost:8080/phigros_sdl_app.html
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
    -DBUILD_RENDER_APP=ON \
    -DBUILD_LEGACY_CLI=OFF \
    -DUSE_BGFX=OFF \
    -DUSE_SDL3=OFF \
    "$@"

echo "[Web] Building…"
emmake cmake --build . --parallel "$(nproc 2>/dev/null || echo 4)" \
    --target phigros_sdl_app

echo ""
echo "[Web] Done → $BUILD_DIR/"
echo "  phigros_sdl_app.js"
echo "  phigros_sdl_app.wasm"
echo "  phigros_sdl_app.html"
echo ""
echo "Serve with:"
echo "  python3 -m http.server 8080 -d $BUILD_DIR"
echo "  open http://localhost:8080/phigros_sdl_app.html"
