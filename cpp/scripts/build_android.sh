#!/usr/bin/env bash
# build_android.sh — Build phigros_render as an Android shared library (.so).
#
# Prerequisites:
#   - Android NDK (r25+) installed
#   - Set ANDROID_NDK_ROOT or ANDROID_NDK environment variable
#     e.g.: export ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/25.2.9519653
#   - ABI: armeabi-v7a, arm64-v8a, x86, x86_64 (default: arm64-v8a)
#   - API level: default 24 (Android 7.0+)
#
# The output is a shared library used by the Gradle project in android/.
# Run this script first, then build the APK with `./gradlew assembleRelease`
# from the android/ directory.
#
# Usage:
#   ./scripts/build_android.sh [ABI] [API_LEVEL] [Release|Debug]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ABI="${1:-arm64-v8a}"
API="${2:-24}"
BUILD_TYPE="${3:-Release}"

# Locate NDK
NDK="${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}"
if [[ -z "$NDK" ]]; then
    # Try common paths
    for try in \
        "$HOME/Android/Sdk/ndk-bundle" \
        "$HOME/Library/Android/sdk/ndk-bundle" \
        "/opt/android-ndk" \
        "/usr/local/android-ndk"; do
        [[ -d "$try" ]] && NDK="$try" && break
    done
fi
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
    echo "[Error] Android NDK not found. Set ANDROID_NDK_ROOT."
    exit 1
fi
echo "[Android] NDK: $NDK"
echo "[Android] ABI: $ABI  API: $API  Build: $BUILD_TYPE"

TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "[Error] android.toolchain.cmake not found at $TOOLCHAIN"
    exit 1
fi

BUILD_DIR="$CPP_DIR/build_android_${ABI}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "[Android] Configuring…"
cmake "$CPP_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DUSE_BGFX=OFF \
    -DUSE_SDL3=OFF \
    -DPHIGROS_ANDROID_LIB=ON

echo "[Android] Building…"
cmake --build . --parallel "$(nproc 2>/dev/null || echo 4)" \
    --target phigros_render

LIB_DIR="$CPP_DIR/android/app/src/main/jniLibs/$ABI"
mkdir -p "$LIB_DIR"
cp -f "$BUILD_DIR"/libphigros_render*.so "$LIB_DIR/" 2>/dev/null || true

echo ""
echo "[Android] Done → $BUILD_DIR/"
echo "  .so copied to android/app/src/main/jniLibs/$ABI/"
echo ""
echo "Next: cd android && ./gradlew assembleDebug"
