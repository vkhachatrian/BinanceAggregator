#pragma once

#include "aggregator.hpp"
#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace binance_aggregator {

class BinanceClient {
public:
    explicit BinanceClient(AppConfig config);

    int run();
    void stop();

private:
    void open_output_file();
    void install_signal_handlers();
    void start_connect();
    void on_resolve(
        boost::beast::error_code error,
        boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code error);
    void on_ssl_handshake(boost::beast::error_code error);
    void on_websocket_handshake(boost::beast::error_code error);
    void read_next();
    void on_read(boost::beast::error_code error, std::size_t bytes_transferred);
    void schedule_flush();
    void on_flush_timer(boost::beast::error_code error);
    void flush_closed_windows();
    void fail(const char* operation, boost::beast::error_code error);
    void request_stop(int exit_code);
    std::string subscription_target() const;

    AppConfig config_;
    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>
        websocket_;
    boost::beast::flat_buffer read_buffer_;
    boost::asio::steady_timer flush_timer_;
    boost::asio::signal_set signals_;
    Aggregator aggregator_;
    std::ofstream output_;
    std::string host_header_;
    std::string target_;
    bool websocket_open_{false};
    bool stopping_{false};
    int exit_code_{0};
    std::uint64_t malformed_payloads_{0};
};

}  // namespace binance_aggregator
