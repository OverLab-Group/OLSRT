# Platform configuration and cross-compilation setup

# Detect host platform
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(HOST_PLATFORM "linux")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(HOST_PLATFORM "windows")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(HOST_PLATFORM "darwin")
elseif(CMAKE_HOST_SYSTEM_NAME MATCHES ".*BSD.*")
    set(HOST_PLATFORM "bsd")
endif()

message(STATUS "Host platform: ${HOST_PLATFORM}")

# Cross-compilation options
option(CROSS_COMPILE "Enable cross-compilation" OFF)
set(TARGET_PLATFORM "${HOST_PLATFORM}" CACHE STRING "Target platform (linux/windows/darwin/bsd)")
set(TARGET_ARCH "x86_64" CACHE STRING "Target architecture (x86_64/i686/aarch64)")

if(CROSS_COMPILE AND NOT HOST_PLATFORM STREQUAL TARGET_PLATFORM)
    message(STATUS "Cross-compiling from ${HOST_PLATFORM} to ${TARGET_PLATFORM}")
    
    if(TARGET_PLATFORM STREQUAL "linux")
        include(cmake/toolchains/linux-gcc.cmake)
    elseif(TARGET_PLATFORM STREQUAL "windows")
        include(cmake/toolchains/windows-mingw.cmake)
    elseif(TARGET_PLATFORM STREQUAL "darwin")
        include(cmake/toolchains/darwin-clang.cmake)
    elseif(TARGET_PLATFORM STREQUAL "bsd")
        include(cmake/toolchains/bsd-clang.cmake)
    endif()
    
    # Set platform defines
    add_definitions(-DOL_PLATFORM_${TARGET_PLATFORM}=1)
else()
    # Native compilation
    if(HOST_PLATFORM STREQUAL "linux")
        add_definitions(-DOL_PLATFORM_LINUX=1)
    elseif(HOST_PLATFORM STREQUAL "windows")
        add_definitions(-DOL_PLATFORM_WINDOWS=1)
    elseif(HOST_PLATFORM STREQUAL "darwin")
        add_definitions(-DOL_PLATFORM_MACOS=1)
    elseif(HOST_PLATFORM STREQUAL "bsd")
        add_definitions(-DOL_PLATFORM_BSD=1)
    endif()
endif()

# Platform-specific libraries
if(TARGET_PLATFORM STREQUAL "linux" OR HOST_PLATFORM STREQUAL "linux")
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS Threads::Threads rt dl)
elseif(TARGET_PLATFORM STREQUAL "windows" OR HOST_PLATFORM STREQUAL "windows")
    set(PLATFORM_LIBS ws2_32 advapi32)
endif()

# Apply platform libraries to target
if(PLATFORM_LIBS)
    target_link_libraries(olsrt ${PLATFORM_LIBS})
endif()