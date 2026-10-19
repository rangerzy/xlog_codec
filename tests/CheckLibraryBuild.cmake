cmake_minimum_required(VERSION 4.2.2)

foreach(required IN ITEMS XLOG_PROJECT XLOG_SOURCE_DIR XLOG_TEST_ROOT XLOG_PLATFORM
        XLOG_C_COMPILER XLOG_CXX_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Command failed: ${ARGV}\n${output}\n${error}")
    endif()
    set(last_output "${output}${error}" PARENT_SCOPE)
endfunction()

# Use an isolated source copy so update tests never edit the real project.
set(work "${XLOG_TEST_ROOT}/${XLOG_PROJECT}")
file(REMOVE_RECURSE "${work}")
set(source "${work}/source")
set(build "${work}/build")
set(prefix "${build}/outputs/${XLOG_PLATFORM}/${XLOG_PROJECT}")
file(MAKE_DIRECTORY "${source}")
foreach(entry IN ITEMS CMakeLists.txt cmake xlog_encode xlog_decode)
    file(COPY "${XLOG_SOURCE_DIR}/${entry}" DESTINATION "${source}")
endforeach()
run_checked("${CMAKE_COMMAND}" -S "${source}" -B "${build}" -G Ninja
    -DBUILD_TESTING=OFF "-DCMAKE_BUILD_TYPE=${XLOG_BUILD_TYPE}"
    "-DCMAKE_C_COMPILER=${XLOG_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${XLOG_CXX_COMPILER}")
run_checked("${CMAKE_COMMAND}" --build "${build}" --target "${XLOG_PROJECT}")

set(metadata
    "include/${XLOG_PROJECT}/${XLOG_PROJECT}.h"
    "include/${XLOG_PROJECT}/${XLOG_PROJECT}_export.h"
    "doc/README.md"
    "lib/cmake/${XLOG_PROJECT}/${XLOG_PROJECT}Config.cmake"
    "lib/cmake/${XLOG_PROJECT}/${XLOG_PROJECT}ConfigVersion.cmake")
foreach(path IN LISTS metadata)
    if(NOT EXISTS "${prefix}/${path}")
        message(FATAL_ERROR "Single-library build did not deliver ${path}")
    endif()
endforeach()
if(XLOG_PROJECT STREQUAL "xlog_encode")
    set(other xlog_decode)
else()
    set(other xlog_encode)
endif()
file(GLOB_RECURSE other_artifacts LIST_DIRECTORIES FALSE
    "${build}/outputs/${XLOG_PLATFORM}/${other}/*")
if(other_artifacts)
    message(FATAL_ERROR "Single-library build also delivered ${other}")
endif()

# Documentation changes must synchronize without relinking the library.
file(APPEND "${source}/${XLOG_PROJECT}/doc/README.md" "\nBuild synchronization check.\n")
run_checked("${CMAKE_COMMAND}" --build "${build}" --target "${XLOG_PROJECT}")
if(last_output MATCHES "Linking")
    message(FATAL_ERROR "Documentation update relinked the library: ${last_output}")
endif()
file(SHA256 "${source}/${XLOG_PROJECT}/doc/README.md" expected)
file(SHA256 "${prefix}/doc/README.md" actual)
if(NOT expected STREQUAL actual)
    message(FATAL_ERROR "Documentation update was not synchronized")
endif()

# Missing delivery files must be restored on an otherwise up-to-date build.
foreach(path IN LISTS metadata)
    file(REMOVE "${prefix}/${path}")
endforeach()
run_checked("${CMAKE_COMMAND}" --build "${build}" --target "${XLOG_PROJECT}")
foreach(path IN LISTS metadata)
    if(NOT EXISTS "${prefix}/${path}")
        message(FATAL_ERROR "Incremental build did not restore ${path}")
    endif()
endforeach()

file(APPEND "${source}/${XLOG_PROJECT}/include/${XLOG_PROJECT}/${XLOG_PROJECT}.h"
    "\n/* Header synchronization check. */\n")
run_checked("${CMAKE_COMMAND}" --build "${build}" --target "${XLOG_PROJECT}")
file(SHA256 "${source}/${XLOG_PROJECT}/include/${XLOG_PROJECT}/${XLOG_PROJECT}.h" expected)
file(SHA256 "${prefix}/include/${XLOG_PROJECT}/${XLOG_PROJECT}.h" actual)
if(NOT expected STREQUAL actual)
    message(FATAL_ERROR "Public header update was not synchronized")
endif()

set(installed "${work}/installed library")
run_checked("${CMAKE_COMMAND}" --install "${build}"
    --component "${XLOG_PROJECT}" --prefix "${installed}")
file(GLOB_RECURSE built_files LIST_DIRECTORIES FALSE RELATIVE "${prefix}" "${prefix}/*")
file(GLOB_RECURSE installed_files LIST_DIRECTORIES FALSE RELATIVE "${installed}" "${installed}/*")
if(NOT built_files STREQUAL installed_files)
    message(FATAL_ERROR "Build/install file lists differ:\n${built_files}\n${installed_files}")
endif()
foreach(path IN LISTS built_files)
    file(SHA256 "${prefix}/${path}" expected)
    file(SHA256 "${installed}/${path}" actual)
    if(NOT expected STREQUAL actual)
        message(FATAL_ERROR "Build/install contents differ: ${path}")
    endif()
endforeach()
run_checked("${CMAKE_COMMAND}" "-DXLOG_PROJECT=${XLOG_PROJECT}"
    "-DXLOG_PREFIX=${installed}" "-DXLOG_TEST_ROOT=${work}/consumer"
    "-DXLOG_BUILD_TYPE=${XLOG_BUILD_TYPE}"
    -P "${XLOG_SOURCE_DIR}/tests/RunPackageConsumer.cmake")
message(STATUS "${XLOG_PROJECT}: single-target build, incremental synchronization and install parity passed")
