#include "trade_parser.hpp"

#include <gtest/gtest.h>

namespace {

using binance_aggregator::Decimal;
using binance_aggregator::parse_trade_payload;

TEST(TradeParser, ParsesRawTradePayload) {
    const auto trade = parse_trade_payload(
        R"({"e":"trade","E":1768227800124,"s":"BTCUSDT","t":12345,"p":"43012.10000000","q":"0.00512000","T":1768227800123})");

    ASSERT_TRUE(trade.has_value());
    EXPECT_EQ(trade->symbol, "BTCUSDT");
    EXPECT_EQ(trade->price, Decimal{"43012.10000000"});
    EXPECT_EQ(trade->quantity, Decimal{"0.00512000"});
    EXPECT_EQ(trade->trade_time_ms, 1768227800123LL);
}

TEST(TradeParser, ParsesCombinedTradePayload) {
    const auto trade = parse_trade_payload(
        R"({"stream":"btcusdt@trade","data":{"e":"trade","E":1768227800124,"s":"BTCUSDT","t":12345,"p":"43012.10000000","q":"0.00512000","T":1768227800123}})");

    ASSERT_TRUE(trade.has_value());
    EXPECT_EQ(trade->symbol, "BTCUSDT");
}

TEST(TradeParser, RejectsMalformedPayload) {
    EXPECT_FALSE(parse_trade_payload("{not-json").has_value());
}

}  // namespace
