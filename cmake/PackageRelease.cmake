cmake_minimum_required(VERSION 4.2.2)

# Run after building both libraries in a single-config Release build:
# cmake -DXLOG_BUILD_DIR=<build> -DXLOG_PLATFORM=macos-arm64 \
#   -P cmake/PackageRelease.cmake
# XLOG_VERSION defaults to the configured project version.
foreach(required IN ITEMS XLOG_BUILD_DIR XLOG_PLATFORM)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
if(NOT XLOG_PLATFORM MATCHES "^(macos-arm64|windows-x64-mingw|linux-x64)$")
    message(FATAL_ERROR "Unsupported XLOG_PLATFORM: ${XLOG_PLATFORM}")
endif()
file(REAL_PATH "${XLOG_BUILD_DIR}" build)
if(NOT EXISTS "${build}/CMakeCache.txt" OR NOT EXISTS "${build}/cmake_install.cmake")
    message(FATAL_ERROR "XLOG_BUILD_DIR must be a configured xlog_codec build directory")
endif()
load_cache("${build}" READ_WITH_PREFIX configured_
    CMAKE_PROJECT_NAME CMAKE_PROJECT_VERSION CMAKE_BUILD_TYPE
    XLOG_BUILD_ENCODE XLOG_BUILD_DECODE)
if(NOT configured_CMAKE_PROJECT_NAME STREQUAL "xlog_codec"
   OR NOT configured_CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Packaging requires an xlog_codec Release build")
endif()
if(NOT configured_XLOG_BUILD_ENCODE OR NOT configured_XLOG_BUILD_DECODE)
    message(FATAL_ERROR "Packaging requires XLOG_BUILD_ENCODE=ON and XLOG_BUILD_DECODE=ON")
endif()
if(NOT DEFINED XLOG_VERSION OR XLOG_VERSION STREQUAL "")
    set(XLOG_VERSION "${configured_CMAKE_PROJECT_VERSION}")
endif()
if(NOT XLOG_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "XLOG_VERSION must be a numeric major.minor.patch version")
endif()
if(NOT configured_CMAKE_PROJECT_VERSION STREQUAL XLOG_VERSION)
    message(FATAL_ERROR
        "XLOG_VERSION=${XLOG_VERSION} differs from build version ${configured_CMAKE_PROJECT_VERSION}")
endif()

if(NOT DEFINED XLOG_OUTPUT_DIR OR XLOG_OUTPUT_DIR STREQUAL "")
    set(XLOG_OUTPUT_DIR "${build}/dist")
endif()
get_filename_component(output "${XLOG_OUTPUT_DIR}" ABSOLUTE)
if(NOT DEFINED XLOG_VERIFY_CONSUMERS)
    set(XLOG_VERIFY_CONSUMERS ON)
endif()

# Never remove a caller-supplied directory. Each invocation owns a fresh work
# directory under the build tree, including failed invocations kept for diagnosis.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(work "${build}/.xlog-release-work/${XLOG_PLATFORM}-${XLOG_VERSION}-${nonce}")
set(stage "${work}/stage")
file(MAKE_DIRECTORY "${stage}")
foreach(project_name IN ITEMS xlog_encode xlog_decode)
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build}"
        --config Release --component "${project_name}" --prefix "${stage}/${project_name}"
        RESULT_VARIABLE result OUTPUT_VARIABLE install_output ERROR_VARIABLE install_error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${project_name} install failed:\n${install_output}\n${install_error}")
    endif()
endforeach()

set(archive_name "xlog-codec-${XLOG_VERSION}-${XLOG_PLATFORM}.zip")
set(archive "${work}/${archive_name}")
file(ARCHIVE_CREATE OUTPUT "${archive}" FORMAT zip
    WORKING_DIRECTORY "${stage}" PATHS xlog_encode xlog_decode)
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DXLOG_ARCHIVE=${archive}"
    "-DXLOG_PLATFORM=${XLOG_PLATFORM}"
    "-DXLOG_VERSION=${XLOG_VERSION}"
    "-DXLOG_TEST_ROOT=${work}/verification"
    "-DXLOG_VERIFY_CONSUMERS=${XLOG_VERIFY_CONSUMERS}"
    -P "${CMAKE_CURRENT_LIST_DIR}/../tests/CheckReleaseArchive.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Release ZIP validation failed:\n${check_output}\n${check_error}")
endif()
message(STATUS "${check_output}")

# Publish only an archive that has passed extraction and SDK validation.
file(MAKE_DIRECTORY "${output}")
file(COPY_FILE "${archive}" "${output}/${archive_name}" ONLY_IF_DIFFERENT)
file(REMOVE_RECURSE "${work}")
message(STATUS "Release ZIP: ${output}/${archive_name}")
