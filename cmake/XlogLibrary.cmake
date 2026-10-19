include_guard(GLOBAL)
include(GenerateExportHeader)

function(xlog_add_library project_name export_name)
    set(prefix "${CMAKE_BINARY_DIR}/outputs/${XLOG_PLATFORM}/${project_name}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_library(${project_name} SHARED "src/${project_name}_api.cpp")
    target_sources(${project_name} PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES FILES "src/${project_name}.cppm")
    add_library(xlog::${export_name} ALIAS ${project_name})
    set_target_properties(${project_name} PROPERTIES
        EXPORT_NAME "${export_name}"
        VERSION "${CMAKE_PROJECT_VERSION}" SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
        LIBRARY_OUTPUT_DIRECTORY "${prefix}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${prefix}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${prefix}/bin")
    string(TOUPPER "${project_name}" macro_prefix)
    generate_export_header(${project_name}
        EXPORT_FILE_NAME "${generated}/${project_name}/${project_name}_export.h"
        EXPORT_MACRO_NAME "${macro_prefix}_EXPORT")
    target_include_directories(${project_name} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${generated}>"
        "$<INSTALL_INTERFACE:include>")
    target_compile_options(${project_name} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-freflection;-Wall;-Wextra;-Wpedantic>")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VersionConfig.h.in"
        "${generated}/xlog_version_config.h" @ONLY)
    if(APPLE)
        set_target_properties(${project_name} PROPERTIES
            INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR YES
            BUILD_WITH_INSTALL_RPATH YES)
    endif()
    add_executable(${project_name}_example "examples/version.c")
    target_link_libraries(${project_name}_example PRIVATE ${project_name})
    set_target_properties(${project_name}_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${prefix}/bin")
    if(APPLE)
        set_target_properties(${project_name}_example PROPERTIES
            INSTALL_RPATH "@loader_path/../lib" BUILD_WITH_INSTALL_RPATH YES)
    endif()
endfunction()
