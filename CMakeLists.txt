cmake_minimum_required(VERSION 3.15)
project(OLSRT LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# Get user input for target and architecture
if(NOT DEFINED TARGET_PLATFORM)
    set(TARGET_PLATFORM "native" CACHE STRING "Target platform (native, linux, windows, darwin, bsd)")
endif()

if(NOT DEFINED TARGET_ARCH)
    set(TARGET_ARCH "x86_64" CACHE STRING "Target architecture (x86_64, i686, aarch64)")
endif()

# Source files
file(GLOB_RECURSE SRC_FILES src/code/streams/*.c)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/includes
    ${CMAKE_SOURCE_DIR}/includes/code/streams
    ${CMAKE_SOURCE_DIR}/includes/runtime
)

# Cross-compilation options
option(CROSS_COMPILE "Cross-compile for different platform" OFF)

# Set output directory
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${TARGET_PLATFORM}/${TARGET_ARCH}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# Create output directory
file(MAKE_DIRECTORY ${OUTPUT_DIR})

# Set platform-specific macros
if(CROSS_COMPILE)
    if(TARGET_PLATFORM STREQUAL "linux")
        add_definitions(-D_LINUX -D__linux__)
    elseif(TARGET_PLATFORM STREQUAL "windows")
        add_definitions(-D_WINDOWS -D_WIN32 -D_WIN64 -DWIN32_LEAN_AND_MEAN)
    elseif(TARGET_PLATFORM STREQUAL "darwin")
        add_definitions(-D_APPLE -D__APPLE__ -D__MACH__)
    elseif(TARGET_PLATFORM STREQUAL "bsd")
        add_definitions(-D_BSD -D__FreeBSD__ -D__BSD__)
    endif()
else()
    # Native compilation
    if(WIN32)
        add_definitions(-D_WINDOWS -D_WIN32 -D_WIN64)
    elseif(APPLE)
        add_definitions(-D_APPLE -D__APPLE__ -D__MACH__)
    elseif(UNIX AND NOT APPLE)
        if(CMAKE_SYSTEM_NAME MATCHES ".*BSD.*")
            add_definitions(-D_BSD -D__FreeBSD__)
        else()
            add_definitions(-D_LINUX -D__linux__)
        endif()
    endif()
endif()

# Create library
add_library(olsrt SHARED ${SRC_FILES})

# Platform-specific settings
if(WIN32 OR (CROSS_COMPILE AND TARGET_PLATFORM STREQUAL "windows"))
    set_target_properties(olsrt PROPERTIES PREFIX "" SUFFIX ".dll")
    target_link_libraries(olsrt ws2_32)
    target_compile_options(olsrt PRIVATE /O2 /W3)
    
elseif(APPLE OR (CROSS_COMPILE AND TARGET_PLATFORM STREQUAL "darwin"))
    set_target_properties(olsrt PROPERTIES SUFFIX ".dylib")
    target_compile_options(olsrt PRIVATE -O3 -Wall -Wextra -fPIC)
    
elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_NAME MATCHES ".*BSD.*" OR 
       (CROSS_COMPILE AND TARGET_PLATFORM STREQUAL "bsd"))
        set_target_properties(olsrt PROPERTIES SUFFIX ".so")
        target_link_libraries(olsrt pthread)
        target_compile_options(olsrt PRIVATE -O3 -Wall -Wextra -fPIC)
    else()
        set_target_properties(olsrt PROPERTIES SUFFIX ".so")
        target_link_libraries(olsrt pthread rt dl)
        target_compile_options(olsrt PRIVATE -O3 -Wall -Wextra -fPIC -flto)
    endif()
endif()

# Print build info
message(STATUS "Building for: ${TARGET_PLATFORM}/${TARGET_ARCH}")
message(STATUS "Output directory: ${OUTPUT_DIR}")