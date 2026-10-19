foreach(project_name IN ITEMS xlog_encode xlog_decode)
    if(NOT TARGET ${project_name})
        continue()
    endif()
    foreach(language IN ITEMS c cpp)
        set(test_name "version_${project_name}_${language}")
        add_executable(${test_name} "version_${language}.${language}")
        target_compile_definitions(${test_name} PRIVATE
            "XLOG_HEADER=\"${project_name}/${project_name}.h\""
            "XLOG_VERSION_FUNCTION=${project_name}_version")
        target_link_libraries(${test_name} PRIVATE ${project_name})
        set_target_properties(${test_name} PROPERTIES
            CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        add_test(NAME ${test_name} COMMAND ${test_name})
        if(WIN32)
            set_tests_properties(${test_name} PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:${project_name}>")
        endif()
    endforeach()
    add_test(NAME relocated_${project_name}
        COMMAND "${CMAKE_COMMAND}"
            "-DXLOG_PROJECT=${project_name}"
            "-DXLOG_PREFIX=${CMAKE_BINARY_DIR}/outputs/${XLOG_PLATFORM}/${project_name}"
            "-DXLOG_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-consumers"
            "-DXLOG_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunPackageConsumer.cmake")
    add_test(NAME build_install_${project_name}
        COMMAND "${CMAKE_COMMAND}"
            "-DXLOG_PROJECT=${project_name}"
            "-DXLOG_PLATFORM=${XLOG_PLATFORM}"
            "-DXLOG_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DXLOG_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/build-install"
            "-DXLOG_CONFIGURED_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DXLOG_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DXLOG_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DXLOG_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckLibraryBuild.cmake")
endforeach()

if(TARGET xlog_decode)
    add_executable(decode_buffer decode_buffer.cpp)
    target_link_libraries(decode_buffer PRIVATE xlog_decode)
    set_target_properties(decode_buffer PROPERTIES CXX_STANDARD 11)
    add_test(NAME decode_buffer COMMAND decode_buffer)
    add_executable(decode_c decode_c.c)
    target_link_libraries(decode_c PRIVATE xlog_decode)
    set_target_properties(decode_c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    add_test(NAME decode_c COMMAND decode_c)
    if(WIN32)
        set_tests_properties(decode_buffer decode_c PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_decode>")
    endif()
endif()

if(TARGET xlog_encode)
    add_executable(encode_buffer encode_buffer.cpp)
    target_link_libraries(encode_buffer PRIVATE xlog_encode ZLIB::ZLIB)
    set_target_properties(encode_buffer PROPERTIES CXX_STANDARD 11)
    add_test(NAME encode_buffer COMMAND encode_buffer)
    add_executable(encode_c encode_c.c)
    target_link_libraries(encode_c PRIVATE xlog_encode)
    set_target_properties(encode_c PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    add_test(NAME encode_c COMMAND encode_c)
    if(WIN32)
        set_tests_properties(encode_buffer encode_c PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_encode>")
    endif()
    if(TARGET xlog_decode)
        add_executable(encode_decode encode_decode.cpp)
        target_link_libraries(encode_decode PRIVATE xlog_encode xlog_decode)
        set_target_properties(encode_decode PROPERTIES CXX_STANDARD 11)
        add_test(NAME encode_decode COMMAND encode_decode)
        if(WIN32)
            set_tests_properties(encode_decode PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_encode>;PATH=path_list_prepend:$<TARGET_FILE_DIR:xlog_decode>")
        endif()
    endif()
endif()
