cmake_minimum_required(VERSION 4.2.2)
project(xlog_codec VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(XlogToolchain)
include(XlogLibrary)
include(CTest)

add_subdirectory(xlog_encode)
add_subdirectory(xlog_decode)

set(xlog_build_examples_default OFF)
if(TARGET xlog_encode AND TARGET xlog_decode)
    set(xlog_build_examples_default ON)
endif()
option(XLOG_BUILD_EXAMPLES "Build examples that use both xlog codecs"
    ${xlog_build_examples_default})

if(XLOG_BUILD_EXAMPLES)
    if(NOT TARGET xlog_encode OR NOT TARGET xlog_decode)
        message(FATAL_ERROR
            "XLOG_BUILD_EXAMPLES=ON requires both xlog_encode and xlog_decode. "
            "Enable XLOG_BUILD_ENCODE=ON and XLOG_BUILD_DECODE=ON, "
            "or set XLOG_BUILD_EXAMPLES=OFF.")
    endif()
    add_subdirectory(example)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
