#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace binance_aggregator {

struct AppConfig {
    std::vector<std::string> symbols;
    std::chrono::milliseconds window;
    std::chrono::milliseconds flush_interval;
    std::filesystem::path output_file;
};

AppConfig load_config(const std::filesystem::path& path);

}  // namespace binance_aggregator
