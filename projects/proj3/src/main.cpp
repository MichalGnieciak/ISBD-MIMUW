#include "./api/server.h"
#include "metastore/metastore.h"
#include "query/executor/executor.h"
#include "query/registry/query_registry.h"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

isbd::api::DbmsServer* g_server = nullptr;

void handle_shutdown_signal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

std::string get_env_or_default(const char* name, const std::string& fallback) {
    const char* val = std::getenv(name);
    if (val != nullptr && *val != '\0') {
        return std::string(val);
    }
    return fallback;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string data_dir = get_env_or_default("ISBD_DATA_DIR", "/data/db");
    std::string host = get_env_or_default("ISBD_HOST", "0.0.0.0");
    int port = std::stoi(get_env_or_default("ISBD_PORT", "8080"));

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--data-dir" || arg == "-d") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        }
    }

    try {
        std::filesystem::create_directories(data_dir);

        isbd::Metastore metastore(data_dir);
        isbd::QueryRegistry registry;
        isbd::Executor executor(metastore, registry);
        isbd::api::DbmsServer server(metastore, registry, executor);

        g_server = &server;
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);

        std::cout << "Starting ISBD DBMS server on " << host << ":" << port
                  << " (data_dir=" << data_dir << ")\n" << std::flush;

        if (!server.listen(host, port)) {
            std::cerr << "Failed to bind HTTP server to " << host << ":" << port << "\n";
            g_server = nullptr;
            return 1;
        }

        g_server = nullptr;
        std::cout << "Shutting down ISBD DBMS server...\n" << std::flush;
        executor.shutdown();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal server error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}