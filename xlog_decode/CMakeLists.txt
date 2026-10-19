project(xlog_decode VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C CXX)

add_library(xlog_decode SHARED src/xlog_decode_api.cpp)
target_sources(xlog_decode PRIVATE
    FILE_SET internal_modules TYPE CXX_MODULES FILES src/xlog_decode.cppm)
xlog_configure_library(xlog_decode decode)
