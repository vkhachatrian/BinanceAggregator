#include "trade_parser.hpp"

#include <nlohmann/json.hpp>

namespace binance_aggregator {

std::optional<Trade> parse_trade_payload(std::string_view payload) {
    const auto json = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (json.is_discarded()) {
        return std::nullopt;
    }

    const nlohmann::json* event = &json;
    if (json.contains("data") && json.at("data").is_object()) {
        event = &json.at("data");
    }

    try {
        if (!event->at("s").is_string() || !event->at("p").is_string() ||
            !event->at("q").is_string() || !event->at("T").is_number_integer()) {
            return std::nullopt;
        }

        return Trade{
            event->at("s").get<std::string>(),
            Decimal{event->at("p").get<std::string>()},
            Decimal{event->at("q").get<std::string>()},
            event->at("T").get<std::int64_t>(),
        };
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace binance_aggregator
