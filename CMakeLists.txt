cmake_minimum_required(VERSION 3.20)
project(ircord-client VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ──────────────────────────────────────────────
# vcpkg dependencies
# ──────────────────────────────────────────────
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(Opus REQUIRED)
find_package(CURL REQUIRED)
find_package(toml11 CONFIG REQUIRED)
find_package(spdlog REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

# ──────────────────────────────────────────────
# FetchContent: libsignal-protocol-c
# ──────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    signal_protocol_c
    GIT_REPOSITORY https://github.com/signalapp/libsignal-protocol-c.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# Use low-level populate so we can patch before add_subdirectory
FetchContent_GetProperties(signal_protocol_c)
if(NOT signal_protocol_c_POPULATED)
    FetchContent_Populate(signal_protocol_c)

    # Patch 1: remove internal_fast_tests.c from curve25519 CMakeLists to fix
    # MSVC VLA compile error in that test file.
    set(_curve_cmake "${signal_protocol_c_SOURCE_DIR}/src/curve25519/CMakeLists.txt")
    if(EXISTS "${_curve_cmake}")
        file(READ "${_curve_cmake}" _curve_content)
        string(REPLACE "ed25519/tests/internal_fast_tests.c" "" _curve_content "${_curve_content}")
        string(REPLACE "internal_fast_tests.c" "" _curve_content "${_curve_content}")
        file(WRITE "${_curve_cmake}" "${_curve_content}")
    endif()

    # Patch 2: stub out curve_internal_fast_tests in curve.c so it no longer
    # calls all_fast_tests (which was defined in the removed test file).
    set(_curve_c "${signal_protocol_c_SOURCE_DIR}/src/curve.c")
    if(EXISTS "${_curve_c}")
        file(READ "${_curve_c}" _curve_c_content)
        string(REPLACE
            "    if (all_fast_tests(silent) != 0)\n        return SG_ERR_UNKNOWN;\n    return 0;"
            "    (void)silent;\n    /* internal_fast_tests.c removed (MSVC VLA incompatible) */\n    return 0;"
            _curve_c_content "${_curve_c_content}")
        file(WRITE "${_curve_c}" "${_curve_c_content}")
    endif()

    add_subdirectory("${signal_protocol_c_SOURCE_DIR}" "${signal_protocol_c_BINARY_DIR}")
endif()

# ──────────────────────────────────────────────
# FetchContent: miniaudio (header-only)
# ──────────────────────────────────────────────
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

if(NOT TARGET miniaudio::miniaudio)
    add_library(miniaudio_hdr INTERFACE)
    target_include_directories(miniaudio_hdr INTERFACE ${miniaudio_SOURCE_DIR})
    add_library(miniaudio::miniaudio ALIAS miniaudio_hdr)
endif()

# ──────────────────────────────────────────────
# Protobuf code generation
# ──────────────────────────────────────────────
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/proto/ircord.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

# ──────────────────────────────────────────────
# Source files
# ──────────────────────────────────────────────
set(CLIENT_SOURCES
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/state/app_state.cpp
    src/ui/ui_manager.cpp
    src/ui/message_view.cpp
    src/ui/input_line.cpp
    src/ui/tab_bar.cpp
    src/ui/status_bar.cpp
    src/ui/link_preview.cpp
    src/net/net_client.cpp
    src/net/message_handler.cpp
    src/crypto/crypto_engine.cpp
    src/crypto/identity.cpp
    src/crypto/signal_store.cpp
    src/crypto/group_session.cpp
    src/voice/voice_engine.cpp
    src/voice/audio_device.cpp
    src/voice/opus_codec.cpp
    src/voice/jitter_buffer.cpp
    src/preview/link_previewer.cpp
    src/db/local_store.cpp
    src/db/migrations.cpp
    src/input/input_handler.cpp
    src/input/command_parser.cpp
    src/input/tab_complete.cpp
    ${PROTO_SRCS}
)

add_executable(ircord-client ${CLIENT_SOURCES})

target_include_directories(ircord-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${signal_protocol_c_SOURCE_DIR}/src
)

target_link_libraries(ircord-client PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    protobuf::libprotobuf
    SQLiteCpp
    unofficial-sodium::sodium
    LibDataChannel::LibDataChannel
    Opus::opus
    CURL::libcurl
    toml11::toml11
    spdlog::spdlog
    signal-protocol-c
    miniaudio::miniaudio
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(MSVC)
    target_compile_options(ircord-client PRIVATE /W3 /wd4996)
else()
    target_compile_options(ircord-client PRIVATE -Wall -Wextra)
endif()

# Copy README to build output directory
add_custom_command(TARGET ircord-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "$<TARGET_FILE_DIR:ircord-client>/README.md"
    COMMENT "Copying README.md to output directory"
)

enable_testing()
