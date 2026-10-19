option(XLOG_BUILD_ENCODE "Build the xlog buffer encoder (requires OpenSSL Crypto and zlib)" ON)
if(NOT XLOG_BUILD_ENCODE)
    return()
endif()

project(xlog_encode VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C CXX)
include(XlogCodecDependencies)

add_library(xlog_encode SHARED src/xlog_encode_api.cpp src/buffer_codec.cpp)
target_sources(xlog_encode PRIVATE
    FILE_SET internal_modules TYPE CXX_MODULES FILES src/xlog_encode.cppm)
xlog_configure_library(xlog_encode encode)
target_link_libraries(xlog_encode PRIVATE OpenSSL::Crypto ZLIB::ZLIB)
if(APPLE)
    # Static dependency archives must not enlarge the encoder's public ABI.
    target_link_options(xlog_encode PRIVATE "LINKER:-exported_symbol,_xlog_encode_*")
elseif(MINGW)
    target_link_options(xlog_encode PRIVATE "LINKER:--exclude-libs,ALL")
endif()
