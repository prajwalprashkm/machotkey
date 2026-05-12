cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_LIST_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
    project(MachotkeyUserBuild LANGUAGES C CXX OBJCXX)
endif()

# Default to optimized user builds unless explicitly overridden.
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo;MinSizeRel;Debug" CACHE STRING "Build configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Release" "RelWithDebInfo" "MinSizeRel" "Debug")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE USER_MACOS_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(USER_MACOS_SDK_PATH STREQUAL "")
    message(FATAL_ERROR "Could not resolve macOS SDK path via xcrun. Install Xcode Command Line Tools.")
endif()
set(CMAKE_OSX_SYSROOT "${USER_MACOS_SDK_PATH}" CACHE PATH "macOS SDK path" FORCE)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)

# Lock this entrypoint to the end-user flow only.
set(USE_PREBUNDLED_UI ON)
set(BUILD_UI_FROM_SOURCE OFF)
set(OFFLINE_ONLY ON)
set(USER_ENABLE_CODESIGN OFF)

enable_language(OBJCXX)

include("${CMAKE_CURRENT_LIST_DIR}/Sources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Frontend.cmake")

user_configure_dependencies("${PROJECT_ROOT}")
user_configure_frontend("${PROJECT_ROOT}" "${CMAKE_BINARY_DIR}")

add_executable(machotkey MACOSX_BUNDLE
    "${PROJECT_ROOT}/project/src/main.cpp"
    ${USER_MACOS_SOURCES}
    ${USER_IMGUI_SOURCES}
)

add_executable(macro_runner
    "${PROJECT_ROOT}/project/src/macro_runner.cpp"
    ${USER_MACOS_SOURCES}
)

set_target_properties(machotkey PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_ROOT}/project/resources/Info.plist"
    MACOSX_BUNDLE_ICON_FILE "machotkey.icns"
    MACOSX_BUNDLE_BUNDLE_NAME "Machotkey"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    OUTPUT_NAME "Machotkey"
)

set_target_properties(macro_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_BUNDLE_DIR:machotkey>/Contents/MacOS"
)

add_dependencies(macro_runner machotkey user_generate_embed user_vite_build)
add_dependencies(machotkey user_generate_embed user_vite_build)

file(GLOB_RECURSE APP_RESOURCES "${PROJECT_ROOT}/project/resources/Resources/*")
target_sources(machotkey PRIVATE ${APP_RESOURCES})
set_source_files_properties(${APP_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

set(APP_ICON "${PROJECT_ROOT}/project/resources/machotkey.icns")
if(EXISTS "${APP_ICON}")
    target_sources(machotkey PRIVATE "${APP_ICON}")
    set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

set(MACOS_FRAMEWORKS
    "ApplicationServices"
    "Carbon"
    "Foundation"
    "CoreGraphics"
    "CoreVideo"
    "QuartzCore"
    "Cocoa"
    "CoreMedia"
    "ScreenCaptureKit"
    "Metal"
    "MetalKit"
    "GameController"
    "Vision"
    "Accelerate"
    "WebKit"
    "UniformTypeIdentifiers"
)

target_compile_options(machotkey PRIVATE -Werror=unguarded-availability-new)
target_compile_options(macro_runner PRIVATE -Werror=unguarded-availability-new)

foreach(target machotkey macro_runner)
    foreach(fw ${MACOS_FRAMEWORKS})
        target_link_libraries(${target} PRIVATE "-framework ${fw}")
    endforeach()

    if(APPLE)
        # OpenCV static component graphs can repeat archives on the link line.
        target_link_options(${target} PRIVATE "-Wl,-no_warn_duplicate_libraries")
    endif()

    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(${target} PRIVATE user_luajit ${USER_OPENCV_LIBS} Eigen3::Eigen)
    target_include_directories(${target} PRIVATE
        "${PROJECT_ROOT}/project/include"
        "${USER_LUAJIT_SRC_DIR}"
        "${USER_SOL2_INCLUDE_DIR}"
        "${USER_IMGUI_INCLUDE_DIR}"
        "${USER_IMGUI_BACKENDS_INCLUDE_DIR}"
        "${USER_EIGEN_INCLUDE_DIR}"
        ${USER_OPENCV_INCLUDE_DIRS}
        "${CMAKE_BINARY_DIR}"
    )
endforeach()

if(TARGET user_build_luajit)
    add_dependencies(machotkey user_build_luajit)
    add_dependencies(macro_runner user_build_luajit)
endif()

user_attach_frontend_to_target(machotkey "${PROJECT_ROOT}" "${CMAKE_BINARY_DIR}")
