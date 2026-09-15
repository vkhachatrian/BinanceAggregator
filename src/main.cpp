#include "binance_client.hpp"
#include "config.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        spdlog::error("Usage: {} <config-file>", argv[0]);
        return 1;
    }

    try {
        auto config = binance_aggregator::load_config(argv[1]);
        spdlog::info(
            "Loaded config: symbols={}, window_ms={}, flush_interval_ms={}, output_file={}",
            config.symbols.size(),
            config.window.count(),
            config.flush_interval.count(),
            config.output_file.string());

        binance_aggregator::BinanceClient client(std::move(config));
        return client.run();
    } catch (const std::exception& ex) {
        spdlog::error("Startup failed: {}", ex.what());
        return 1;
    }
}
