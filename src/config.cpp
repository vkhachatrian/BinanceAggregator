#include "config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace binance_aggregator {

AppConfig load_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open config file: " + path.string());
    }

    nlohmann::json json;
    input >> json;

    return AppConfig{
        json.at("symbols").get<std::vector<std::string>>(),
        std::chrono::milliseconds{json.at("window_ms").get<long long>()},
        std::chrono::milliseconds{json.at("flush_interval_ms").get<long long>()},
        std::filesystem::path{json.at("output_file").get<std::string>()},
    };
}

}  // namespace binance_aggregator
