include_guard(GLOBAL)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

function(xlog_configure_library project_name export_name)
    set(prefix "${CMAKE_BINARY_DIR}/outputs/${XLOG_PLATFORM}/${project_name}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")

    # Shared-library properties and the ordinary header interface.
    add_library(xlog::${export_name} ALIAS ${project_name})
    set_target_properties(${project_name} PROPERTIES
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

    # One relocatable Config is used by both build output and installation.
    set(runtime_directory lib)
    if(WIN32)
        set(runtime_directory bin)
        set(link_property IMPORTED_IMPLIB)
        set(link_value "\${PACKAGE_PREFIX_DIR}/lib/$<TARGET_LINKER_FILE_NAME:${project_name}>")
    else()
        set(link_property IMPORTED_SONAME)
        set(link_value "@rpath/$<TARGET_SONAME_FILE_NAME:${project_name}>")
    endif()
    configure_package_config_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LibraryConfig.cmake.in"
        "${generated}/${project_name}Config.cmake.in"
        INSTALL_DESTINATION "lib/cmake/${project_name}")
    file(GENERATE OUTPUT "${generated}/${project_name}Config.cmake"
        INPUT "${generated}/${project_name}Config.cmake.in")
    write_basic_package_version_file("${generated}/${project_name}ConfigVersion.cmake"
        VERSION "${CMAKE_PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)

    # The same file lists feed the build copy rule and install rules.
    set(headers "${CMAKE_CURRENT_SOURCE_DIR}/include/${project_name}/${project_name}.h"
        "${generated}/${project_name}/${project_name}_export.h")
    set(document "${CMAKE_CURRENT_SOURCE_DIR}/doc/README.md")
    set(configs "${generated}/${project_name}Config.cmake"
        "${generated}/${project_name}ConfigVersion.cmake")
    set(delivered_files
        "${prefix}/include/${project_name}/${project_name}.h"
        "${prefix}/include/${project_name}/${project_name}_export.h"
        "${prefix}/doc/README.md"
        "${prefix}/lib/cmake/${project_name}/${project_name}Config.cmake"
        "${prefix}/lib/cmake/${project_name}/${project_name}ConfigVersion.cmake")
    add_custom_command(OUTPUT ${delivered_files}
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${prefix}/include/${project_name}" "${prefix}/doc" "${prefix}/lib/cmake/${project_name}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${headers} "${prefix}/include/${project_name}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${document}" "${prefix}/doc"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${configs} "${prefix}/lib/cmake/${project_name}"
        DEPENDS ${headers} "${document}" ${configs}
        COMMENT "Synchronizing ${project_name} headers, documentation and CMake config"
        VERBATIM)
    set_source_files_properties(${delivered_files} PROPERTIES HEADER_FILE_ONLY YES)
    target_sources(${project_name} PRIVATE ${delivered_files})

    install(TARGETS ${project_name}
        RUNTIME DESTINATION bin COMPONENT ${project_name}
        LIBRARY DESTINATION lib COMPONENT ${project_name} NAMELINK_COMPONENT ${project_name}
        ARCHIVE DESTINATION lib COMPONENT ${project_name})
    install(FILES ${headers} DESTINATION "include/${project_name}" COMPONENT ${project_name})
    install(FILES "${document}" DESTINATION doc COMPONENT ${project_name})
    install(FILES ${configs} DESTINATION "lib/cmake/${project_name}" COMPONENT ${project_name})
endfunction()
