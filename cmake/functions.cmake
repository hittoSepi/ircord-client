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

function(add_release_zip_target TARGET_NAME)
    cmake_parse_arguments(ARG "" "ARCHIVE_NAME;OUTPUT_DIR" "EXTRA_FILES" ${ARGN})

    if(NOT ARG_ARCHIVE_NAME)
        set(ARG_ARCHIVE_NAME "${PROJECT_NAME}-latest.zip")
    endif()

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/release")
    endif()

    set(_archive_path "${ARG_OUTPUT_DIR}/${ARG_ARCHIVE_NAME}")
    set(_target_name "${TARGET_NAME}_release_zip")

    add_custom_command(
        OUTPUT "${_archive_path}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ARG_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${_archive_path}" --format=zip
            "$<TARGET_FILE_NAME:${TARGET_NAME}>"
            ${ARG_EXTRA_FILES}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        DEPENDS ${TARGET_NAME}
        COMMENT "Packing release archive ${ARG_ARCHIVE_NAME}"
        VERBATIM
    )

    add_custom_target(${_target_name} DEPENDS "${_archive_path}")
endfunction()

function(add_ssh_upload_target UPLOAD_TARGET)
    cmake_parse_arguments(
        ARG
        ""
        "FILE;HOST;REMOTE_PATH;USER;PORT;IDENTITY_FILE"
        "DEPENDS"
        ${ARGN}
    )

    if(NOT ARG_FILE)
        message(FATAL_ERROR "add_ssh_upload_target requires FILE")
    endif()

    if(NOT ARG_HOST)
        message(FATAL_ERROR "add_ssh_upload_target requires HOST")
    endif()

    if(NOT ARG_REMOTE_PATH)
        message(FATAL_ERROR "add_ssh_upload_target requires REMOTE_PATH")
    endif()

    find_program(SCP_EXECUTABLE scp)
    if(NOT SCP_EXECUTABLE)
        message(FATAL_ERROR "scp command not found. Install OpenSSH client to use ${UPLOAD_TARGET}.")
    endif()

    if(ARG_USER)
        set(_remote_dest "${ARG_USER}@${ARG_HOST}:${ARG_REMOTE_PATH}")
    else()
        set(_remote_dest "${ARG_HOST}:${ARG_REMOTE_PATH}")
    endif()

    set(_scp_command "${SCP_EXECUTABLE}")
    if(ARG_PORT)
        list(APPEND _scp_command -P "${ARG_PORT}")
    endif()
    if(ARG_IDENTITY_FILE)
        list(APPEND _scp_command -i "${ARG_IDENTITY_FILE}")
    endif()
    list(APPEND _scp_command "${ARG_FILE}" "${_remote_dest}")

    add_custom_target(${UPLOAD_TARGET}
        COMMAND ${_scp_command}
        DEPENDS ${ARG_DEPENDS}
        COMMENT "Uploading ${ARG_FILE} -> ${_remote_dest}"
        VERBATIM
    )
endfunction()
