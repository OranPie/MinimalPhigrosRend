# Mobile build configurations for iOS and Android.
# Usage:
#   Android: cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#                  -DANDROID_ABI=arm64-v8a -DANDROID_NATIVE_API_LEVEL=24 ..
#   iOS:     cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
#                  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAM_ID> ..

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PHIGROS_MOBILE ON)
    set(PHIGROS_ANDROID ON)
    # Android NDK provides the toolchain; bgfx auto-selects GLES3/Vulkan
    add_definitions(-DPHIGROS_ANDROID=1)
    # Disable audio backends that don't work on Android
    set(MA_NO_PULSEAUDIO ON)
    set(MA_NO_JACK ON)
    set(MA_NO_ALSA ON)
    # Disable display drivers incompatible with Android
    set(MA_NO_RUNTIME_LINKING OFF)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PHIGROS_MOBILE ON)
    set(PHIGROS_IOS ON)
    add_definitions(-DPHIGROS_IOS=1)
    set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2") # iPhone + iPad
    # Enforce Metal on iOS; disable OpenGL ES fallback paths
    add_definitions(-DPHIGROS_METAL=1)
    # Disable audio backends not available on iOS
    set(MA_NO_PULSEAUDIO ON)
    set(MA_NO_JACK ON)
    set(MA_NO_ALSA ON)
    set(MA_NO_OSS ON)
    set(MA_NO_DSOUND ON)
    set(MA_NO_WINMM ON)
    # bgfx auto-selects Metal on iOS
endif()

if(PHIGROS_MOBILE)
    add_definitions(-DPHIGROS_MOBILE=1)
    # Touch input is primary on mobile; lower flick detection threshold for small screens
    add_definitions(-DPHIGROS_TOUCH_INPUT=1)
    add_definitions(-DPHIGROS_FLICK_THRESHOLD_PX_S=900)
endif()
