#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <boost/multiprecision/cpp_dec_float.hpp>

namespace binance_aggregator {

using Decimal = boost::multiprecision::cpp_dec_float_50;

struct Trade {
    std::string symbol;
    Decimal price;
    Decimal quantity;
    std::int64_t trade_time_ms;
};

std::optional<Trade> parse_trade_payload(std::string_view payload);

}  // namespace binance_aggregator
