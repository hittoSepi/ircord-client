#include "app.hpp"
#include <iostream>
#include <string>
#include <filesystem>

static void print_help(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>   Path to client.toml (default: platform config dir)\n"
              << "  --user   <id>     Override user_id from config\n"
              << "  --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::filesystem::path config_path;
    std::string           user_id_override;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "--user" || arg == "-u") && i + 1 < argc) {
            user_id_override = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    // Default config path
    if (config_path.empty()) {
        config_path = ircord::default_config_dir() / "client.toml";
    }

    ircord::App app;
    if (!app.init(config_path, user_id_override)) {
        std::cerr << "Application initialization failed.\n";
        return 1;
    }

    return app.run();
}
