cmake_minimum_required(VERSION 3.15)
project(OLSRT C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE SRC_FILES src/code/streams/*.c)

include_directories(
    ${CMAKE_SOURCE_DIR}/includes
    ${CMAKE_SOURCE_DIR}/includes/code/streams
    ${CMAKE_SOURCE_DIR}/includes/runtime
)

add_library(olsrt SHARED ${SRC_FILES})

if(WIN32)
    target_compile_definitions(olsrt PRIVATE _WINDOWS)
    target_link_libraries(olsrt ws2_32)
elseif(APPLE)
    set_target_properties(olsrt PROPERTIES SUFFIX ".dylib")
    target_compile_definitions(olsrt PRIVATE _APPLE)
elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
        target_compile_definitions(olsrt PRIVATE _BSD)
    else()
        target_compile_definitions(olsrt PRIVATE _LINUX)
        target_link_libraries(olsrt pthread)
    endif()
endif()

target_compile_options(olsrt PRIVATE -O3 -Wall -fPIC -flto)