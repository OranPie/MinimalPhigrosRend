# Mobile build configurations for iOS and Android.
# Usage:
#   Android: cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#                  -DANDROID_ABI=arm64-v8a -DANDROID_NATIVE_API_LEVEL=24 ..
#   iOS:     cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 ..

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PHIGROS_MOBILE ON)
    set(PHIGROS_ANDROID ON)
    # Android NDK provides the toolchain; bgfx auto-selects GLES3/Vulkan
    add_definitions(-DPHIGROS_ANDROID=1)
    # Disable audio backends that don't work on Android
    set(MA_NO_PULSEAUDIO ON)
    set(MA_NO_JACK ON)
    set(MA_NO_ALSA ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PHIGROS_MOBILE ON)
    set(PHIGROS_IOS ON)
    add_definitions(-DPHIGROS_IOS=1)
    set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2") # iPhone + iPad
    # bgfx auto-selects Metal on iOS
endif()

if(PHIGROS_MOBILE)
    add_definitions(-DPHIGROS_MOBILE=1)
    # Touch input is primary on mobile
    add_definitions(-DPHIGROS_TOUCH_INPUT=1)
endif()
