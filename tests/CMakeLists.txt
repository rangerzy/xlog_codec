foreach(project_name IN ITEMS xlog_encode xlog_decode)
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
            "-DXLOG_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DXLOG_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DXLOG_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckLibraryBuild.cmake")
endforeach()
