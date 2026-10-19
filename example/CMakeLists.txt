project(example LANGUAGES CXX)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE xlog_encode xlog_decode)
set_target_properties(example PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    CXX_SCAN_FOR_MODULES NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(example PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_test(NAME example_roundtrip COMMAND example)
    if(WIN32)
        set_tests_properties(example_roundtrip PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_encode>;PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_decode>")
    endif()
endif()
