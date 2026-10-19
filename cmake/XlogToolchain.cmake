include_guard(GLOBAL)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "xlog_codec requires GNU GCC 16 or newer with C++26 reflection support.")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "xlog_codec requires single-config Ninja; use a separate build directory for each configuration.")
endif()
execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
    OUTPUT_VARIABLE ninja_version OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(ninja_version VERSION_LESS 1.11)
    message(FATAL_ERROR "xlog_codec requires Ninja 1.11 or newer.")
endif()
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"
   AND (NOT CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    set(XLOG_PLATFORM macos-arm64)
elseif(WIN32 AND MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 8
       AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    set(XLOG_PLATFORM windows-x64-mingw)
else()
    message(FATAL_ERROR "Supported platforms: macOS ARM64 GCC, Windows x64 MinGW GCC (UCRT64).")
endif()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "-freflection")
check_cxx_source_compiles("#include <meta>
    static_assert(std::meta::is_type(^^int));
    int main() { return 0; }" XLOG_HAS_REFLECTION)
cmake_pop_check_state()
if(NOT XLOG_HAS_REFLECTION)
    message(FATAL_ERROR "This GCC toolchain cannot compile C++26 <meta> reflection with -freflection.")
endif()
