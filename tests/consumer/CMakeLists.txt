cmake_minimum_required(VERSION 4.2.2)
project(xlog_consumer LANGUAGES C CXX)
# Keep the package under test ahead of stale SDKs in the host's include paths.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
find_package(${XLOG_PROJECT} CONFIG REQUIRED)
string(REPLACE "xlog_" "" export_name "${XLOG_PROJECT}")
set(library "xlog::${export_name}")

foreach(property IN ITEMS IMPORTED_LOCATION IMPORTED_IMPLIB)
    get_target_property(location ${library} ${property})
    if(location)
        cmake_path(IS_PREFIX XLOG_PREFIX "${location}" NORMALIZE inside_package)
        if(NOT inside_package OR NOT EXISTS "${location}")
            message(FATAL_ERROR "Non-relocatable imported artifact: ${location}")
        endif()
    endif()
endforeach()

if(XLOG_PROJECT STREQUAL "xlog_decode")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/decode_cpp.cpp" "#include \"decode_c.c\"\n")
    add_executable(consumer_decode_c decode_c.c)
    add_executable(consumer_decode_cpp "${CMAKE_CURRENT_BINARY_DIR}/decode_cpp.cpp")
    foreach(consumer IN ITEMS consumer_decode_c consumer_decode_cpp)
        target_include_directories(${consumer} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${consumer} PRIVATE ${library})
        set_target_properties(${consumer} PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED YES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
    endforeach()
endif()

if(XLOG_PROJECT STREQUAL "xlog_encode")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/encode_cpp.cpp" "#include \"encode_c.c\"\n")
    add_executable(consumer_encode_c encode_c.c)
    add_executable(consumer_encode_cpp "${CMAKE_CURRENT_BINARY_DIR}/encode_cpp.cpp")
    foreach(consumer IN ITEMS consumer_encode_c consumer_encode_cpp)
        target_include_directories(${consumer} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${consumer} PRIVATE ${library})
        set_target_properties(${consumer} PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED YES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
    endforeach()
endif()

foreach(language IN ITEMS c cpp)
    add_executable(consumer_${language} "version_${language}.${language}")
    target_compile_definitions(consumer_${language} PRIVATE
        "XLOG_HEADER=\"${XLOG_PROJECT}/${XLOG_PROJECT}.h\""
        "XLOG_VERSION_FUNCTION=${XLOG_PROJECT}_version")
    target_link_libraries(consumer_${language} PRIVATE ${library})
    set_target_properties(consumer_${language} PROPERTIES
        CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
endforeach()
