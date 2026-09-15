#include "binance_client.hpp"

#include "trade_parser.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cctype>
#include <filesystem>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>

namespace binance_aggregator {

namespace {

constexpr char kBinanceHost[] = "stream.binance.com";
constexpr char kBinancePort[] = "9443";

std::int64_t current_epoch_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

}  // namespace

BinanceClient::BinanceClient(AppConfig config)
    : config_(std::move(config)),
      ssl_context_(boost::asio::ssl::context::tlsv12_client),
      resolver_(io_context_),
      websocket_(io_context_, ssl_context_),
      flush_timer_(io_context_),
      signals_(io_context_, SIGINT, SIGTERM),
      aggregator_(config_.window) {
    if (config_.symbols.empty()) {
        throw std::invalid_argument("symbols must not be empty");
    }
    if (config_.flush_interval.count() <= 0) {
        throw std::invalid_argument("flush_interval_ms must be positive");
    }

    ssl_context_.set_verify_mode(boost::asio::ssl::verify_peer);
    boost::beast::error_code error;
    ssl_context_.set_default_verify_paths(error);
    if (error) {
        throw boost::beast::system_error{error};
    }
}

int BinanceClient::run() {
    open_output_file();
    install_signal_handlers();
    start_connect();

    io_context_.run();

    if (output_.is_open()) {
        output_.flush();
    }

    spdlog::info(
        "Service stopped: malformed_payloads={}, dropped_late_trades={}",
        malformed_payloads_,
        aggregator_.dropped_late_trades());

    return exit_code_;
}

void BinanceClient::stop() {
    request_stop(0);
}

void BinanceClient::open_output_file() {
    const auto parent_path = config_.output_file.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }

    output_.open(config_.output_file, std::ios::app);
    if (!output_) {
        throw std::runtime_error("failed to open output file: " + config_.output_file.string());
    }
}

void BinanceClient::install_signal_handlers() {
    signals_.async_wait([this](boost::beast::error_code error, int signal_number) {
        if (error) {
            return;
        }

        spdlog::info("Received signal {}, shutting down", signal_number);
        stop();
    });
}

void BinanceClient::start_connect() {
    host_header_ = std::string{kBinanceHost} + ":" + kBinancePort;
    target_ = subscription_target();

    spdlog::info("Connecting to wss://{}{} for {} symbols", host_header_, target_, config_.symbols.size());
    resolver_.async_resolve(
        kBinanceHost,
        kBinancePort,
        [this](
            boost::beast::error_code error,
            boost::asio::ip::tcp::resolver::results_type results) {
            on_resolve(error, std::move(results));
        });
}

void BinanceClient::on_resolve(
    boost::beast::error_code error,
    boost::asio::ip::tcp::resolver::results_type results) {
    if (error) {
        fail("resolve", error);
        return;
    }

    boost::beast::get_lowest_layer(websocket_).expires_after(std::chrono::seconds(30));
    boost::beast::get_lowest_layer(websocket_).async_connect(
        results,
        [this](
            boost::beast::error_code connect_error,
            boost::asio::ip::tcp::resolver::results_type::endpoint_type) {
            on_connect(connect_error);
        });
}

void BinanceClient::on_connect(boost::beast::error_code error) {
    if (error) {
        fail("connect", error);
        return;
    }

    if (!SSL_set_tlsext_host_name(websocket_.next_layer().native_handle(), kBinanceHost)) {
        fail(
            "SNI setup",
            boost::beast::error_code{
                static_cast<int>(ERR_get_error()),
                boost::asio::error::get_ssl_category()});
        return;
    }

    websocket_.next_layer().async_handshake(
        boost::asio::ssl::stream_base::client,
        [this](boost::beast::error_code handshake_error) {
            on_ssl_handshake(handshake_error);
        });
}

void BinanceClient::on_ssl_handshake(boost::beast::error_code error) {
    if (error) {
        fail("TLS handshake", error);
        return;
    }

    boost::beast::get_lowest_layer(websocket_).expires_never();
    websocket_.set_option(boost::beast::websocket::stream_base::timeout::suggested(
        boost::beast::role_type::client));
    websocket_.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& request) {
            request.set(
                boost::beast::http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) + " binance-aggregator");
        }));

    websocket_.async_handshake(
        host_header_,
        target_,
        [this](boost::beast::error_code handshake_error) {
            on_websocket_handshake(handshake_error);
        });
}

void BinanceClient::on_websocket_handshake(boost::beast::error_code error) {
    if (error) {
        fail("WebSocket handshake", error);
        return;
    }

    websocket_open_ = true;
    spdlog::info("Subscribed to Binance combined stream {}", target_);
    schedule_flush();
    read_next();
}

void BinanceClient::read_next() {
    if (stopping_) {
        return;
    }

    websocket_.async_read(
        read_buffer_,
        [this](boost::beast::error_code error, std::size_t bytes_transferred) {
            on_read(error, bytes_transferred);
        });
}

void BinanceClient::on_read(boost::beast::error_code error, std::size_t) {
    if (error) {
        if (stopping_ || error == boost::asio::error::operation_aborted) {
            return;
        }
        if (error == boost::beast::websocket::error::closed) {
            websocket_open_ = false;
        }
        fail("WebSocket read", error);
        return;
    }

    const auto payload = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());

    const auto trade = parse_trade_payload(payload);
    if (!trade.has_value()) {
        ++malformed_payloads_;
        spdlog::warn("Skipped malformed or unexpected payload: {}", payload);
        read_next();
        return;
    }

    if (!aggregator_.add_trade(*trade)) {
        spdlog::warn(
            "Dropped late trade: symbol={}, trade_time_ms={}, dropped_late_trades={}",
            trade->symbol,
            trade->trade_time_ms,
            aggregator_.dropped_late_trades());
    }

    read_next();
}

void BinanceClient::schedule_flush() {
    if (stopping_) {
        return;
    }

    flush_timer_.expires_after(config_.flush_interval);
    flush_timer_.async_wait([this](boost::beast::error_code error) {
        on_flush_timer(error);
    });
}

void BinanceClient::on_flush_timer(boost::beast::error_code error) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            fail("flush timer", error);
        }
        return;
    }

    flush_closed_windows();
    schedule_flush();
}

void BinanceClient::flush_closed_windows() {
    const auto completed = aggregator_.flush_closed(current_epoch_ms());
    const auto text = serialize_completed_windows(completed);
    if (text.empty()) {
        return;
    }

    output_ << text;
    output_.flush();
    if (!output_) {
        spdlog::error("Failed to write output file {}", config_.output_file.string());
        exit_code_ = 1;
        stopping_ = true;
        io_context_.stop();
        return;
    }

    spdlog::info("Flushed {} aggregate row(s)", completed.size());
}

void BinanceClient::fail(const char* operation, boost::beast::error_code error) {
    if (stopping_ && error == boost::asio::error::operation_aborted) {
        return;
    }

    spdlog::error("{} failed: {}", operation, error.message());
    request_stop(1);
}

void BinanceClient::request_stop(int exit_code) {
    if (exit_code_ == 0) {
        exit_code_ = exit_code;
    }

    if (stopping_) {
        return;
    }

    stopping_ = true;
    signals_.cancel();
    resolver_.cancel();
    flush_timer_.cancel();
    flush_closed_windows();

    if (websocket_open_ && websocket_.is_open()) {
        websocket_.async_close(
            boost::beast::websocket::close_code::normal,
            [this](boost::beast::error_code error) {
                websocket_open_ = false;
                if (error && error != boost::asio::error::operation_aborted) {
                    spdlog::warn("WebSocket close failed: {}", error.message());
                }
                io_context_.stop();
            });
        return;
    }

    io_context_.stop();
}

std::string BinanceClient::subscription_target() const {
    std::ostringstream target;
    target << "/stream?streams=";

    for (std::size_t index = 0; index < config_.symbols.size(); ++index) {
        if (index != 0) {
            target << '/';
        }
        target << lowercase(config_.symbols[index]) << "@trade";
    }

    return target.str();
}

}  // namespace binance_aggregator
