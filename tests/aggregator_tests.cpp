#include "aggregator.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

using binance_aggregator::Aggregator;
using binance_aggregator::Decimal;
using binance_aggregator::Trade;
using binance_aggregator::serialize_completed_windows;

Trade make_trade(
    std::string symbol,
    const char* price,
    const char* quantity,
    std::int64_t trade_time_ms) {
    return Trade{std::move(symbol), Decimal{price}, Decimal{quantity}, trade_time_ms};
}

TEST(Aggregator, KeepsConfiguredWindowSize) {
    const binance_aggregator::Aggregator aggregator(std::chrono::milliseconds{60000});

    EXPECT_EQ(aggregator.window_size(), std::chrono::milliseconds{60000});
}

TEST(Aggregator, RejectsNonPositiveWindowSize) {
    EXPECT_THROW(
        binance_aggregator::Aggregator(std::chrono::milliseconds{0}),
        std::invalid_argument);
}

TEST(Aggregator, TradeExactlyOnBoundaryBelongsToNextWindow) {
    Aggregator aggregator(std::chrono::milliseconds{10000});

    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "100", "1", 9999)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "200", "1", 10000)));

    const auto first_flush = aggregator.flush_closed(10000);
    ASSERT_EQ(first_flush.size(), 1U);
    EXPECT_EQ(first_flush[0].window_start_ms, 0);
    EXPECT_EQ(first_flush[0].statistics.trades, 1U);
    EXPECT_EQ(first_flush[0].statistics.volume, Decimal{"100"});
    EXPECT_EQ(aggregator.active_window_count(), 1U);

    const auto second_flush = aggregator.flush_closed(20000);
    ASSERT_EQ(second_flush.size(), 1U);
    EXPECT_EQ(second_flush[0].window_start_ms, 10000);
    EXPECT_EQ(second_flush[0].statistics.trades, 1U);
    EXPECT_EQ(second_flush[0].statistics.volume, Decimal{"200"});
}

TEST(Aggregator, AggregatesTradesVolumeMinAndMax) {
    Aggregator aggregator(std::chrono::milliseconds{10000});

    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "100", "2", 1000)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "105", "3", 3000)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "98", "1", 7000)));

    const auto windows = aggregator.flush_closed(10000);
    ASSERT_EQ(windows.size(), 1U);
    EXPECT_EQ(windows[0].symbol, "BTCUSDT");
    EXPECT_EQ(windows[0].statistics.trades, 3U);
    EXPECT_EQ(windows[0].statistics.volume, Decimal{"613"});
    EXPECT_EQ(windows[0].statistics.min_price, Decimal{"98"});
    EXPECT_EQ(windows[0].statistics.max_price, Decimal{"105"});
}

TEST(Aggregator, SkipsWindowsWithZeroTradesWhenSerializing) {
    Aggregator aggregator(std::chrono::milliseconds{10000});

    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "100", "1", 1000)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("ETHUSDT", "200", "1", 20000)));

    const auto text = serialize_completed_windows(aggregator.flush_closed(30000));

    EXPECT_NE(text.find("timestamp=1970-01-01T00:00:00Z"), std::string::npos);
    EXPECT_EQ(text.find("timestamp=1970-01-01T00:00:10Z"), std::string::npos);
    EXPECT_NE(text.find("timestamp=1970-01-01T00:00:20Z"), std::string::npos);
}

TEST(Aggregator, SerializesWindowsInRequiredFormat) {
    Aggregator aggregator(std::chrono::milliseconds{10000});

    ASSERT_TRUE(aggregator.add_trade(make_trade("ETHUSDT", "200", "2", 5000)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "100", "2", 1000)));
    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "105", "3", 3000)));

    const auto text = serialize_completed_windows(aggregator.flush_closed(10000));

    EXPECT_EQ(
        text,
        "timestamp=1970-01-01T00:00:00Z\n"
        "symbol=BTCUSDT trades=2 volume=515.00000000 min=100.00000000 max=105.00000000\n"
        "symbol=ETHUSDT trades=1 volume=400.00000000 min=200.00000000 max=200.00000000\n");
}

TEST(Aggregator, DropsTradesForAlreadyFlushedWindows) {
    Aggregator aggregator(std::chrono::milliseconds{10000});

    ASSERT_TRUE(aggregator.add_trade(make_trade("BTCUSDT", "100", "1", 1000)));
    ASSERT_EQ(aggregator.flush_closed(20000).size(), 1U);

    EXPECT_FALSE(aggregator.add_trade(make_trade("BTCUSDT", "101", "1", 5000)));
    EXPECT_EQ(aggregator.dropped_late_trades(), 1U);
    EXPECT_EQ(aggregator.active_window_count(), 0U);
}

}  // namespace
