project(xlog_encode VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C CXX)

add_library(xlog_encode SHARED src/xlog_encode_api.cpp)
target_sources(xlog_encode PRIVATE
    FILE_SET internal_modules TYPE CXX_MODULES FILES src/xlog_encode.cppm)
xlog_configure_library(xlog_encode encode)
