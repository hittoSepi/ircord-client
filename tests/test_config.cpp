#include "config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace {

std::filesystem::path make_temp_dir(const char* tag) {
    const auto tick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      (std::string("ircord-") + tag + "-" + std::to_string(tick));
    std::filesystem::create_directories(path);
    return path;
}

void cleanup(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

} // namespace

TEST_CASE("voice ICE settings survive save/load", "[config]") {
    const auto dir = make_temp_dir("config-save");
    const auto path = dir / "client.toml";

    ircord::ClientConfig cfg;
    cfg.voice.ice_servers = {
        "stun:turn.example.com:3478",
        "turn:turn.example.com:3478?transport=udp",
        "turns:turn.example.com:5349?transport=tcp",
    };
    cfg.voice.turn_username = "ircord";
    cfg.voice.turn_password = "secret";

    ircord::save_config(cfg, path);
    const auto loaded = ircord::load_config(path);

    REQUIRE(loaded.voice.ice_servers == cfg.voice.ice_servers);
    REQUIRE(loaded.voice.turn_username == cfg.voice.turn_username);
    REQUIRE(loaded.voice.turn_password == cfg.voice.turn_password);

    cleanup(dir);
}

TEST_CASE("voice ICE settings survive export/import", "[config]") {
    const auto dir = make_temp_dir("config-export");
    const auto path = dir / "settings.toml";

    ircord::ClientConfig original;
    original.voice.ice_servers = {
        "stun:turn.example.com:3478",
        "turn:turn.example.com:3478?transport=udp",
    };
    original.voice.turn_username = "relay";
    original.voice.turn_password = "relay-secret";

    ircord::export_settings(original, path);

    ircord::ClientConfig imported;
    REQUIRE(ircord::import_settings(imported, path));
    REQUIRE(imported.voice.ice_servers == original.voice.ice_servers);
    REQUIRE(imported.voice.turn_username == original.voice.turn_username);
    REQUIRE(imported.voice.turn_password == original.voice.turn_password);

    cleanup(dir);
}
