option(XLOG_BUILD_DECODE "Build the xlog buffer decoder (requires OpenSSL Crypto and zlib)" ON)
if(NOT XLOG_BUILD_DECODE)
    return()
endif()

project(xlog_decode VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C CXX)
include(XlogCodecDependencies)

add_library(xlog_decode SHARED src/xlog_decode_api.cpp src/buffer_codec.cpp)
target_sources(xlog_decode PRIVATE
    FILE_SET internal_modules TYPE CXX_MODULES FILES src/xlog_decode.cppm)
xlog_configure_library(xlog_decode decode)
target_link_libraries(xlog_decode PRIVATE OpenSSL::Crypto ZLIB::ZLIB)
if(APPLE)
    # Static dependency archives must not enlarge the decoder's public ABI.
    target_link_options(xlog_decode PRIVATE "LINKER:-exported_symbol,_xlog_decode_*")
elseif(MINGW)
    target_link_options(xlog_decode PRIVATE "LINKER:--exclude-libs,ALL")
endif()
