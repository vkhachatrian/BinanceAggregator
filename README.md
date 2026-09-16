# Binance Market Data Aggregator

Linux C++17 service that connects to the public Binance Spot WebSocket API, subscribes to `@trade` streams for a configurable set of symbols, aggregates per-symbol statistics in tumbling exchange-time windows, and periodically appends closed windows to a plain-text file.

## Tested Environment

Fill in after building on Linux:

- Distro: Ubuntu 24.04.1 LTS
- Compiler: g++ 13.3.0
- CMake: 3.28.3
- Conan: 2.32.0

## Build

From a clean checkout on Linux:

```bash
conan profile detect --force
conan install . --output-folder=build --build=missing -s build_type=Release
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Language standard: **C++17** (`CMAKE_CXX_STANDARD 17`).

Dependencies (Conan): Boost.Beast/Asio, OpenSSL, nlohmann/json, spdlog, GoogleTest.

## Run

```bash
./build/binance_aggregator config/example.json
```

The only command-line argument is the configuration file. The example config writes to `output/aggregates.txt` in append mode. Stop with Ctrl-C (SIGINT) or SIGTERM.

Example configuration keys: `symbols`, `window_ms`, `flush_interval_ms`, `output_file`.

## Architecture

The process is split so aggregation can be tested without network I/O.

| Component | Role |
|---|---|
| `main` | Validates `argc`, loads config, constructs the client, returns its exit code. |
| `config` | Reads JSON into `AppConfig`. |
| `binance_client` | TLS WebSocket to `wss://stream.binance.com:9443`, flush timer, signals, append-only output file. |
| `trade_parser` | Turns raw or combined `@trade` JSON into a `Trade` (`s`, `p`, `q`, `T`). Malformed payloads become empty. |
| `aggregator` | Tumbling windows keyed by `(window_start_ms, symbol)`, late-trade watermark, output serialization. |

`binance_aggregator_core` is the library used by both the service and unit tests. `BinanceClient` links only into the `binance_aggregator` executable.

### Threading model

A **single Boost.Asio `io_context` thread** owns:

- DNS, TCP, TLS, and WebSocket handshake
- `async_read` of each frame
- the `flush_interval_ms` `steady_timer`
- SIGINT/SIGTERM via `signal_set`

`Aggregator` is only touched from those callbacks, so the hot path has no mutexes.

### Data flow

1. Load config from `argv[1]`.
2. Open `output_file` in append mode; subscribe to a combined stream `/stream?streams=<symbol>@trade/...`.
3. Parse each payload. Unexpected JSON is logged and skipped; aggregates are left unchanged.
4. Bucket trades by exchange timestamp `T`: `window_start_ms = (T / window_ms) * window_ms` (integer division). A trade exactly on a boundary belongs to the next window.
5. Per `(symbol, window)` keep `trades`, quote-asset `volume` (`Σ price * quantity`), `min`, and `max`.
6. Every `flush_interval_ms` of wall-clock time, append windows whose end is in the past, ordered by `(window_start, symbol)`. Symbols with zero trades in a window emit no line. Trades for an already-flushed window are dropped, counted, and logged.
7. On SIGINT/SIGTERM: flush closed windows, close the socket and file, exit `0`.

## Known Limitations

Reconnection, metrics, databases, Docker/systemd, order-book/kline/REST streams, benchmarking, and log rotation are out of scope for this task. For real 24/7 use:

- **Binance 24h disconnect.** Spot streams drop the socket about every 24 hours. The service currently treats that as a fatal read error and exits non-zero. Production should reconnect with exponential backoff, resubscribe, and keep in-memory windows that have not been flushed.
- **TCP/TLS failures.** Resolve, connect, handshake, and mid-stream resets also stop the process. A supervisor (systemd/`restart=on-failure`) plus the backoff above would cover this.
- **Clock skew.** Window *assignment* uses exchange `T`; *closing* a window uses local wall clock. If the host clock is behind Binance, a window can stay open too long; if it is ahead, a window can flush before the exchange has finished that interval, and later trades are dropped as late.
- **Open window on SIGINT.** Shutdown flushes only windows that have already closed. The current incomplete window is discarded. Production could snapshot it as a partial row or keep it across a restart.
- **Disk full / write errors.** A failed append stops the service. There is no rotation, back-pressure, or alternate sink.
- **No persistence of live state.** Restarting loses unflushed windows. A write-ahead log or republish from a durable queue would be needed for exactly-once-ish recovery.
