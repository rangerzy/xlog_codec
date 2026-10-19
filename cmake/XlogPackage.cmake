include_guard(GLOBAL)
include(CMakePackageConfigHelpers)

function(xlog_add_package project_name export_name)
    set(destination "lib/cmake/${project_name}")
    set(prefix "${CMAKE_BINARY_DIR}/outputs/${XLOG_PLATFORM}/${project_name}")
    install(TARGETS ${project_name} EXPORT ${project_name}Targets
        RUNTIME DESTINATION bin COMPONENT ${project_name}
        LIBRARY DESTINATION lib COMPONENT ${project_name} NAMELINK_COMPONENT ${project_name}
        ARCHIVE DESTINATION lib COMPONENT ${project_name})
    install(TARGETS ${project_name}_example RUNTIME DESTINATION bin COMPONENT ${project_name})
    install(FILES "include/${project_name}/${project_name}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${project_name}/${project_name}_export.h"
        DESTINATION "include/${project_name}" COMPONENT ${project_name})
    install(FILES doc/README.md DESTINATION doc COMPONENT ${project_name})
    configure_package_config_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LibraryConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${project_name}Config.cmake"
        INSTALL_DESTINATION "${destination}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/${project_name}ConfigVersion.cmake"
        VERSION "${CMAKE_PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${project_name}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${project_name}ConfigVersion.cmake"
        DESTINATION "${destination}" COMPONENT ${project_name})
    install(EXPORT ${project_name}Targets NAMESPACE xlog::
        DESTINATION "${destination}" COMPONENT ${project_name})
    add_custom_target(package_${project_name} ALL
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --component "${project_name}" --prefix "${prefix}"
        DEPENDS ${project_name} ${project_name}_example
        COMMENT "Packaging ${project_name} into ${prefix}"
        VERBATIM)
endfunction()
