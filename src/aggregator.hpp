#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "trade_parser.hpp"

namespace binance_aggregator {

struct WindowKey {
    std::int64_t window_start_ms{};
    std::string symbol;

    bool operator<(const WindowKey& other) const;
};

struct WindowStatistics {
    std::uint64_t trades{0};
    Decimal volume{0};
    Decimal min_price{0};
    Decimal max_price{0};
};

struct CompletedWindow {
    std::int64_t window_start_ms{};
    std::string symbol;
    WindowStatistics statistics;
};

class Aggregator {
public:
    explicit Aggregator(std::chrono::milliseconds window_size);

    std::chrono::milliseconds window_size() const noexcept;
    std::int64_t window_start_for(std::int64_t trade_time_ms) const noexcept;
    bool add_trade(const Trade& trade);
    std::vector<CompletedWindow> flush_closed(std::int64_t now_ms);
    std::size_t active_window_count() const noexcept;
    std::uint64_t dropped_late_trades() const noexcept;

private:
    std::chrono::milliseconds window_size_;
    std::map<WindowKey, WindowStatistics> windows_;
    std::int64_t flushed_through_ms_;
    std::uint64_t dropped_late_trades_{0};
};

std::string format_decimal(const Decimal& value);
std::string format_timestamp_utc(std::int64_t epoch_ms);
std::string serialize_completed_windows(const std::vector<CompletedWindow>& windows);

}  // namespace binance_aggregator
