# iOS app target — wraps the phigros_render shared library in an iOS .app bundle.
#
# Build:
#   cmake -G Xcode \
#         -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
#         -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAM_ID> \
#         -DSDL2_DIR=<path/to/SDL2.framework/..> \
#         ..
#   xcodebuild -scheme PhigrosRenderer -configuration Release
#
# Requires: SDL2 compiled for iOS (XCFramework or individual device/simulator frameworks).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif()

set(_IOS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ios/PhigrosRenderer")

add_executable(PhigrosRendererApp MACOSX_BUNDLE
    "${_IOS_SRC_DIR}/main.m"
    "${_IOS_SRC_DIR}/PhigrosAppDelegate.m"
    "${_IOS_SRC_DIR}/PhigrosAppDelegate.h"
)

# Link the core engine library
target_link_libraries(PhigrosRendererApp PRIVATE phigros_render)

# ── Bundle settings ───────────────────────────────────────────────────────────
set_target_properties(PhigrosRendererApp PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${_IOS_SRC_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.phigros.renderer"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"   # iPhone + iPad
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    OUTPUT_NAME "PhigrosRenderer"
)

# ── SDL2 framework (required for iOS) ────────────────────────────────────────
find_library(SDL2_FRAMEWORK SDL2
    PATHS "${SDL2_DIR}" ENV SDL2_DIR
    PATH_SUFFIXES "Frameworks" "xcframeworks/ios-arm64"
    NO_DEFAULT_PATH
)
if(SDL2_FRAMEWORK)
    target_link_libraries(PhigrosRendererApp PRIVATE "${SDL2_FRAMEWORK}")
    set_target_properties(PhigrosRendererApp PROPERTIES
        XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "$(inherited) ${SDL2_DIR}"
    )
else()
    message(WARNING "SDL2.framework not found — set SDL2_DIR to point at the SDL2 XCFramework parent directory.")
endif()

# ── Required iOS system frameworks ───────────────────────────────────────────
target_link_libraries(PhigrosRendererApp PRIVATE
    "-framework UIKit"
    "-framework Foundation"
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework CoreMotion"
    "-framework CoreHaptics"
    "-framework GameController"
    "-framework AVFoundation"
    "-framework AudioToolbox"
)

install(TARGETS PhigrosRendererApp BUNDLE DESTINATION .)
