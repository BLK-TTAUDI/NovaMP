// server/src/main.cpp
// NovaMP Master Server entry point

#include <csignal>
#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <toml++/toml.hpp>

#include "master_server.hpp"

namespace fs = std::filesystem;

static std::unique_ptr<novaMP::MasterServer> g_server;

static void signal_handler(int sig) {
    spdlog::info("Signal {} received — shutting down...", sig);
    if (g_server) g_server->stop();
}

static void setup_logging(const std::string& level, const std::string& log_file) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, 10 * 1024 * 1024, 3);

    auto logger = std::make_shared<spdlog::logger>(
        "novaMP", spdlog::sinks_init_list{console_sink, file_sink});

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    if      (level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "warn")  spdlog::set_level(spdlog::level::warn);
    else if (level == "error") spdlog::set_level(spdlog::level::err);
    else                       spdlog::set_level(spdlog::level::info);
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.toml";
    if (argc > 1) config_path = argv[1];

    toml::table cfg;
    try {
        cfg = toml::parse_file(config_path);
    } catch (const toml::parse_error& e) {
        std::cerr << "Failed to parse config: " << e.what() << "\n";
        return 1;
    }

    auto log_level = cfg["server"]["log_level"].value_or<std::string>("info");
    auto log_file  = cfg["server"]["log_file"].value_or<std::string>("master.log");

    fs::create_directories("data");
    setup_logging(log_level, log_file);

    spdlog::info("NovaMP Master Server v1.0.0 starting...");

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGHUP,  signal_handler);
#endif

    try {
        g_server = std::make_unique<novaMP::MasterServer>(cfg);
        g_server->run();
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }

    spdlog::info("NovaMP Master Server stopped.");
    return 0;
}
