#include "aggregator.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace binance_aggregator {

bool WindowKey::operator<(const WindowKey& other) const {
    if (window_start_ms != other.window_start_ms) {
        return window_start_ms < other.window_start_ms;
    }

    return symbol < other.symbol;
}

Aggregator::Aggregator(std::chrono::milliseconds window_size)
    : window_size_(window_size),
      flushed_through_ms_(std::numeric_limits<std::int64_t>::min()) {
    if (window_size_.count() <= 0) {
        throw std::invalid_argument("window_size must be positive");
    }
}

std::chrono::milliseconds Aggregator::window_size() const noexcept {
    return window_size_;
}

std::int64_t Aggregator::window_start_for(std::int64_t trade_time_ms) const noexcept {
    const auto window_ms = window_size_.count();
    return (trade_time_ms / window_ms) * window_ms;
}

bool Aggregator::add_trade(const Trade& trade) {
    const auto window_start_ms = window_start_for(trade.trade_time_ms);
    if (window_start_ms < flushed_through_ms_) {
        ++dropped_late_trades_;
        return false;
    }

    auto& statistics = windows_[WindowKey{window_start_ms, trade.symbol}];
    if (statistics.trades == 0) {
        statistics.min_price = trade.price;
        statistics.max_price = trade.price;
    } else {
        statistics.min_price = std::min(statistics.min_price, trade.price);
        statistics.max_price = std::max(statistics.max_price, trade.price);
    }

    ++statistics.trades;
    statistics.volume += trade.price * trade.quantity;
    return true;
}

std::vector<CompletedWindow> Aggregator::flush_closed(std::int64_t now_ms) {
    const auto flush_cutoff_ms = window_start_for(now_ms);
    std::vector<CompletedWindow> completed;

    auto current = windows_.begin();
    while (current != windows_.end() && current->first.window_start_ms < flush_cutoff_ms) {
        completed.push_back(CompletedWindow{
            current->first.window_start_ms,
            current->first.symbol,
            current->second,
        });
        current = windows_.erase(current);
    }

    if (flush_cutoff_ms > flushed_through_ms_) {
        flushed_through_ms_ = flush_cutoff_ms;
    }

    return completed;
}

std::size_t Aggregator::active_window_count() const noexcept {
    return windows_.size();
}

std::uint64_t Aggregator::dropped_late_trades() const noexcept {
    return dropped_late_trades_;
}

std::string format_decimal(const Decimal& value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(8) << value;
    return output.str();
}

std::string format_timestamp_utc(std::int64_t epoch_ms) {
    const auto seconds = static_cast<std::time_t>(epoch_ms / 1000);
    std::tm utc{};

#ifdef _WIN32
    if (gmtime_s(&utc, &seconds) != 0) {
        throw std::runtime_error("failed to convert timestamp to UTC");
    }
#else
    if (gmtime_r(&seconds, &utc) == nullptr) {
        throw std::runtime_error("failed to convert timestamp to UTC");
    }
#endif

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string serialize_completed_windows(const std::vector<CompletedWindow>& windows) {
    auto sorted = windows;
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.window_start_ms != rhs.window_start_ms) {
            return lhs.window_start_ms < rhs.window_start_ms;
        }

        return lhs.symbol < rhs.symbol;
    });

    std::ostringstream output;
    std::int64_t current_window_start_ms = std::numeric_limits<std::int64_t>::min();

    for (const auto& window : sorted) {
        if (window.statistics.trades == 0) {
            continue;
        }

        if (window.window_start_ms != current_window_start_ms) {
            current_window_start_ms = window.window_start_ms;
            output << "timestamp=" << format_timestamp_utc(current_window_start_ms) << '\n';
        }

        output << "symbol=" << window.symbol
               << " trades=" << window.statistics.trades
               << " volume=" << format_decimal(window.statistics.volume)
               << " min=" << format_decimal(window.statistics.min_price)
               << " max=" << format_decimal(window.statistics.max_price)
               << '\n';
    }

    return output.str();
}

}  // namespace binance_aggregator
