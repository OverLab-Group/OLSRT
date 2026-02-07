# ==============================================================================
# OLSRT - CMake Build Configuration
# ==============================================================================
# This is the CMake build system configuration for OLSRT, providing IDE-friendly
# cross-platform compilation with support for multiple generators.
# ==============================================================================

cmake_minimum_required(VERSION 3.12)
project(OLSRT 
    VERSION 1.0.0
    DESCRIPTION "OLSRT Runtime Library"
    LANGUAGES C
    HOMEPAGE_URL "https://github.com/username/olsrt"
)

# ------------------------------------------------------------------------------
# PROJECT METADATA AND CONFIGURATION
# ------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable position independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------------------
# PLATFORM DETECTION AND CONFIGURATION
# ------------------------------------------------------------------------------
message(STATUS "🔍 Detecting platform...")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Processor: ${CMAKE_SYSTEM_PROCESSOR}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM "linux")
    add_definitions(-D_LINUX -D__linux__)
    message(STATUS "  Platform: Linux")
    
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM "windows")
    add_definitions(-D_WINDOWS -D_WIN32 -DWIN32_LEAN_AND_MEAN)
    message(STATUS "  Platform: Windows")
    
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM "darwin")
    add_definitions(-D_APPLE -D__APPLE__ -D__MACH__)
    set(CMAKE_MACOSX_RPATH ON)
    message(STATUS "  Platform: macOS")
    
elseif(CMAKE_SYSTEM_NAME MATCHES "BSD")
    set(PLATFORM "bsd")
    add_definitions(-D_BSD -D__BSD__)
    message(STATUS "  Platform: BSD")
    
else()
    message(WARNING "⚠️  Unknown platform: ${CMAKE_SYSTEM_NAME}")
    set(PLATFORM "unknown")
endif()

# ------------------------------------------------------------------------------
# ARCHITECTURE DETECTION
# ------------------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(ARCH "aarch64")
    else()
        set(ARCH "x86_64")
    endif()
else()
    set(ARCH "i686")
endif()
message(STATUS "  Architecture: ${ARCH}")

# ------------------------------------------------------------------------------
# COMPILER FLAGS CONFIGURATION
# ------------------------------------------------------------------------------
# Common compiler flags
set(COMMON_FLAGS "-O3 -Wall -Wextra")

# Platform-specific flags
if(PLATFORM STREQUAL "linux")
    set(PLATFORM_FLAGS "-fPIC")
    set(PLATFORM_LIBS pthread rt dl)
    
elseif(PLATFORM STREQUAL "windows")
    set(PLATFORM_FLAGS "")
    set(PLATFORM_LIBS ws2_32)
    
elseif(PLATFORM STREQUAL "darwin")
    set(PLATFORM_FLAGS "-fPIC")
    set(PLATFORM_LIBS "")
    
elseif(PLATFORM STREQUAL "bsd")
    set(PLATFORM_FLAGS "-fPIC")
    set(PLATFORM_LIBS pthread)
endif()

# Set compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS} ${PLATFORM_FLAGS}")

# ------------------------------------------------------------------------------
# SOURCE FILES CONFIGURATION
# ------------------------------------------------------------------------------
# Find all C source files
file(GLOB_RECURSE SRC_FILES 
    "src/code/streams/*.c"
)

if(NOT SRC_FILES)
    message(FATAL_ERROR "❌ No source files found in src/code/streams/")
endif()

message(STATUS "📁 Found ${SRC_FILES} source files")

# ------------------------------------------------------------------------------
# INCLUDE DIRECTORIES
# ------------------------------------------------------------------------------
include_directories(
    includes
    includes/code
    includes/code/streams
    includes/runtime
)

message(STATUS "📂 Include directories configured")

# ------------------------------------------------------------------------------
# TARGET DEFINITION - SHARED LIBRARY
# ------------------------------------------------------------------------------
# Create shared library
add_library(olsrt SHARED ${SRC_FILES})

# Set target properties
set_target_properties(olsrt PROPERTIES
    OUTPUT_NAME "olsrt"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    DEBUG_POSTFIX "_d"
)

# Link platform libraries
target_link_libraries(olsrt ${PLATFORM_LIBS})

# Set output directory
set_target_properties(olsrt PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${PLATFORM}/${ARCH}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${PLATFORM}/${ARCH}"
)

message(STATUS "📦 Library target 'olsrt' created")

# ------------------------------------------------------------------------------
# OPTIONAL: STATIC LIBRARY TARGET
# ------------------------------------------------------------------------------
option(BUILD_STATIC "Build static library" OFF)
if(BUILD_STATIC)
    add_library(olsrt_static STATIC ${SRC_FILES})
    set_target_properties(olsrt_static PROPERTIES
        OUTPUT_NAME "olsrt"
        POSITION_INDEPENDENT_CODE ON
    )
    target_link_libraries(olsrt_static ${PLATFORM_LIBS})
    message(STATUS "📦 Static library target created")
endif()

# ------------------------------------------------------------------------------
# CROSS-COMPILATION SUPPORT
# ------------------------------------------------------------------------------
option(ENABLE_CROSS_COMPILE "Enable cross-compilation" OFF)
if(ENABLE_CROSS_COMPILE)
    message(STATUS "🌍 Cross-compilation enabled")
    # Note: Toolchain files should be provided for cross-compilation
endif()

# ------------------------------------------------------------------------------
# TESTING SUPPORT
# ------------------------------------------------------------------------------
option(BUILD_TESTS "Build test executables" OFF)
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
    message(STATUS "🧪 Test configuration enabled")
endif()

# ------------------------------------------------------------------------------
# INSTALLATION CONFIGURATION
# ------------------------------------------------------------------------------
# Install library
install(TARGETS olsrt
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY includes/
    DESTINATION include/olsrt
    FILES_MATCHING PATTERN "*.h"
)

# Generate pkg-config file
option(GENERATE_PKGCONFIG "Generate pkg-config file" ON)
if(GENERATE_PKGCONFIG)
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/olsrt-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/olsrt-config.cmake"
        INSTALL_DESTINATION lib/cmake/olsrt
    )
    message(STATUS "📄 pkg-config file generation enabled")
endif()

# ------------------------------------------------------------------------------
# CUSTOM TARGETS
# ------------------------------------------------------------------------------
# Target to build for all platforms (requires toolchains)
add_custom_target(all-platforms
    COMMENT "Building OLSRT for all platforms..."
    DEPENDS olsrt
)

# Display build information
add_custom_target(build-info
    COMMAND ${CMAKE_COMMAND} -E echo "OLSRT Build Information:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Platform: ${PLATFORM}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Architecture: ${ARCH}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Build Type: ${CMAKE_BUILD_TYPE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Source Files: ${SRC_FILES}"
    VERBATIM
)

# ------------------------------------------------------------------------------
# GENERATOR-SPECIFIC CONFIGURATION
# ------------------------------------------------------------------------------
# Multi-configuration generators (Visual Studio, Xcode)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;MinSizeRel;RelWithDebInfo")
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING
        "Reset the configurations to what we need" FORCE)
endif()

# ------------------------------------------------------------------------------
# POST-BUILD MESSAGES
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "✅ OLSRT CMake configuration complete!")
message(STATUS "========================================")
message(STATUS "To build:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  -DCMAKE_BUILD_TYPE=Debug|Release|RelWithDebInfo|MinSizeRel")
message(STATUS "  -DBUILD_STATIC=ON|OFF")
message(STATUS "  -DBUILD_TESTS=ON|OFF")
message(STATUS "  -DGENERATE_PKGCONFIG=ON|OFF")
message(STATUS "========================================")

# ==============================================================================
# END OF CMAKE CONFIGURATION
# ==============================================================================