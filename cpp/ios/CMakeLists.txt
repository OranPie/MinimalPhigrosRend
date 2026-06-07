if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif()

enable_language(Swift)

set(_IOS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhigrosRenderer")
set(_ROOT_RESPACK "${CMAKE_CURRENT_SOURCE_DIR}/../../respack.zip")

add_executable(PhigrosRendererApp MACOSX_BUNDLE
    # Entry point
    "${_IOS_SRC_DIR}/PhigrosMobileApp.swift"
    # Bridging header (C++ → Swift)
    "${_IOS_SRC_DIR}/PhigrosRenderer-Bridging-Header.h"
    # Models
    "${_IOS_SRC_DIR}/Models/PlayMode.swift"
    "${_IOS_SRC_DIR}/Models/GameBridge.swift"
    "${_IOS_SRC_DIR}/Models/ChartLibrary.swift"
    "${_IOS_SRC_DIR}/Models/GameplaySettings.swift"
    "${_IOS_SRC_DIR}/Models/ChartMediaResolver.swift"
    # Views (SwiftUI navigation + settings)
    "${_IOS_SRC_DIR}/Views/ChartLibraryView.swift"
    "${_IOS_SRC_DIR}/Views/PlayModeSheet.swift"
    "${_IOS_SRC_DIR}/Views/SettingsView.swift"
    "${_IOS_SRC_DIR}/Views/ResultView.swift"
    # Game layer (UIKit + Metal)
    "${_IOS_SRC_DIR}/Game/GameViewController.swift"
    "${_IOS_SRC_DIR}/Game/GameMetalView.swift"
    "${_IOS_SRC_DIR}/Game/MetalRenderer.swift"
    "${_IOS_SRC_DIR}/Game/MetalRespack.swift"
    "${_IOS_SRC_DIR}/Game/GameHUDView.swift"
    "${_IOS_SRC_DIR}/Game/PauseMenuView.swift"
    "${_IOS_SRC_DIR}/Game/Shaders.metal"
    "${_ROOT_RESPACK}"
)

set_source_files_properties("${_ROOT_RESPACK}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

target_include_directories(PhigrosRendererApp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)
target_link_libraries(PhigrosRendererApp PRIVATE phigros_mobile_bridge)

set_target_properties(PhigrosRendererApp PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${_IOS_SRC_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.phigros.renderer"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.9"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${_IOS_SRC_DIR}/PhigrosRenderer-Bridging-Header.h"
    # Metal shader settings — ensure output is default.metallib so makeDefaultLibrary() finds it
    XCODE_ATTRIBUTE_MTL_ENABLE_DEBUG_INFO "INCLUDE_SOURCE"
    XCODE_ATTRIBUTE_MTL_LANGUAGE_REVISION "Metal30"
    XCODE_ATTRIBUTE_MTL_FAST_MATH "YES"
    OUTPUT_NAME "PhigrosRenderer"
)

target_link_libraries(PhigrosRendererApp PRIVATE
    "-framework Foundation"
    "-framework SwiftUI"
    "-framework UIKit"
    "-framework MetalKit"
    "-framework Metal"
    "-framework UniformTypeIdentifiers"
    # C++ runtime — required because the static C++ libs use exceptions
    # but the app target contains only Swift sources.
    "c++"
)

install(TARGETS PhigrosRendererApp BUNDLE DESTINATION .)
