cmake_minimum_required(VERSION 4.2.2)
foreach(required IN ITEMS XLOG_PROJECT XLOG_PREFIX XLOG_TEST_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

set(work "${XLOG_TEST_ROOT}/${XLOG_PROJECT}")
set(prefix "${work}/moved package")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/source")
file(COPY "${XLOG_PREFIX}/" DESTINATION "${prefix}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/consumer/" DESTINATION "${work}/source")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/version_c.c"
    "${CMAKE_CURRENT_LIST_DIR}/version_cpp.cpp" DESTINATION "${work}/source")

file(GLOB_RECURSE module_artifacts "${prefix}/*.cppm" "${prefix}/*.gcm"
    "${prefix}/*.pcm" "${prefix}/*.o" "${prefix}/*.obj")
if(module_artifacts)
    message(FATAL_ERROR "Internal module artifacts leaked into package: ${module_artifacts}")
endif()

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Command failed: ${ARGV}\n${output}\n${error}")
    endif()
    set(last_error "${error}" PARENT_SCOPE)
endfunction()

run_checked("${CMAKE_COMMAND}" -S "${work}/source" -B "${work}/build" -G Ninja
    "-DCMAKE_BUILD_TYPE=${XLOG_BUILD_TYPE}"
    "-DCMAKE_PREFIX_PATH=${prefix}" "-DXLOG_PREFIX=${prefix}"
    "-DXLOG_PROJECT=${XLOG_PROJECT}")
run_checked("${CMAKE_COMMAND}" --build "${work}/build")
foreach(language IN ITEMS c cpp)
    set(executable "${work}/build/consumer_${language}")
    if(CMAKE_HOST_WIN32)
        string(APPEND executable ".exe")
        run_checked("${CMAKE_COMMAND}" -E env --modify
            "PATH=path_list_prepend:${prefix}/bin" -- "${executable}")
    elseif(CMAKE_HOST_APPLE)
        run_checked("${CMAKE_COMMAND}" -E env DYLD_PRINT_LIBRARIES=1 "${executable}")
        string(FIND "${last_error}" "${prefix}/lib/" loaded_from_package)
        if(loaded_from_package EQUAL -1)
            message(FATAL_ERROR "Consumer did not load the relocated dylib:\n${last_error}")
        endif()
    else()
        run_checked("${executable}")
    endif()
endforeach()
message(STATUS "${XLOG_PROJECT}: relocated C and C++11 consumers passed")
