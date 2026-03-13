##   cmake/functions.cmake
##   Helpers CMake functions and tools

function(string_split INPUT DELIM OUTPUT)
    string(REPLACE "${DELIM}" ";" _result "${INPUT}")
    set(${OUTPUT} "${_result}" PARENT_SCOPE)
endfunction()

function(generate_version_header VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_STRING)
    set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/version.hpp")

    file(WRITE "${VERSION_HEADER}" "// Auto-generated version header - DO NOT EDIT\n")
    file(APPEND "${VERSION_HEADER}" "#pragma once\n")
    file(APPEND "${VERSION_HEADER}" "#include <string_view>\n\n")
    file(APPEND "${VERSION_HEADER}" "namespace ircord {\n")
    file(APPEND "${VERSION_HEADER}" "inline constexpr std::string_view VERSION_MAJOR = \"${VERSION_MAJOR}\";\n")
    file(APPEND "${VERSION_HEADER}" "inline constexpr std::string_view VERSION_MINOR = \"${VERSION_MINOR}\";\n")
    file(APPEND "${VERSION_HEADER}" "inline constexpr std::string_view VERSION_PATCH = \"${VERSION_PATCH}\";\n")
    file(APPEND "${VERSION_HEADER}" "inline constexpr std::string_view VERSION = \"${VERSION_STRING}\";\n")
    file(APPEND "${VERSION_HEADER}" "} // namespace ircord\n")

    message(STATUS "Generated version header: ${VERSION_HEADER}")
endfunction()

function(generate_version)
    set(PATCH_COUNT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_count")

    if(NOT EXISTS "${PATCH_COUNT_PATH}")
        file(WRITE "${PATCH_COUNT_PATH}" "0.20.0\n")
    endif()

    file(READ "${PATCH_COUNT_PATH}" PATCH_COUNT)
    string(STRIP "${PATCH_COUNT}" PATCH_COUNT)

    if(NOT PATCH_COUNT MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "Invalid version format in ${PATCH_COUNT_PATH}: '${PATCH_COUNT}'")
    endif()

    string(REPLACE "." ";" VERSION_LIST "${PATCH_COUNT}")
    list(GET VERSION_LIST 0 VERSION_MAJOR)
    list(GET VERSION_LIST 1 VERSION_MINOR)
    list(GET VERSION_LIST 2 VERSION_PATCH)

    math(EXPR VERSION_PATCH "${VERSION_PATCH} + 1")
    set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

    file(WRITE "${PATCH_COUNT_PATH}" "${PROJECT_VERSION}\n")

    set(PROJECT_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
    set(PROJECT_VERSION_MAJOR "${VERSION_MAJOR}" PARENT_SCOPE)
    set(PROJECT_VERSION_MINOR "${VERSION_MINOR}" PARENT_SCOPE)
    set(PROJECT_VERSION_PATCH "${VERSION_PATCH}" PARENT_SCOPE)

    generate_version_header(
        "${VERSION_MAJOR}"
        "${VERSION_MINOR}"
        "${VERSION_PATCH}"
        "${PROJECT_VERSION}"
    )

    message(STATUS "Project version: ${PROJECT_VERSION}")
endfunction()
