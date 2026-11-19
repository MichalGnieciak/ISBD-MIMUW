#include <httplib.h>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main(int argc, char* argv[]) {

    // Env var support
    const char* env_dir = std::getenv("ISBD_DATA_DIR");
    std::string data_dir = env_dir ? env_dir : "/data/db";
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data-dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
    }

    std::filesystem::create_directories(data_dir);
    auto start_time = std::chrono::steady_clock::now();

    httplib::Server svr;

    svr.Get("/system/info", [&](const httplib::Request&, httplib::Response& res) {
        auto now = std::chrono::steady_clock::now();
        int64_t uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        json body = {
            {"interfaceVersion", "2.1.0"},
            {"version", "1.0.0"},
            {"author", "MichalGnieciak"},
            {"uptime", uptime}
        };
        res.set_content(body.dump(), "application/json");
    });

    std::cout << "Starting ISBD server on port " << port << " with data dir: " << data_dir << "\n";
    svr.listen("0.0.0.0", port);
    return 0;
}