# Binance Market Data Aggregator

Linux C++ service for collecting Binance Spot `@trade` WebSocket events, aggregating per-symbol statistics in tumbling exchange-time windows, and appending closed windows to a plain-text output file.

This repository is currently at Phase 2: project skeleton. The source layout, build system, dependency wiring, example config, and initial test target are present; the WebSocket service and full aggregation/output behavior still need to be implemented before submission.

## Requirements Source

The implementation follows the attached interview task PDF as the product specification:

- C++17 or newer. This project uses C++17.
- Linux target platform.
- CMake + Conan build.
- Symbols, window size, flush interval, and output path must come from one config file passed as the only command-line argument.
- Aggregation logic must be testable without network I/O.
- Unit tests must run through `ctest` or one documented command.
- Production reconnection, metrics endpoints, databases, Docker/systemd packaging, order-book streams, REST API, benchmarking, and output rotation are out of scope.

## Tested Environment

To be filled after the first WSL build:

- Distro: TODO
- Compiler: TODO
- CMake: TODO
- Conan: TODO

## Build

From a clean checkout on Linux:

```bash
conan profile detect --force
conan install . --output-folder=build --build=missing -s build_type=Release
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/binance_aggregator config/example.json
```

The final service must open `output_file` in append mode. The example configuration writes to `output/aggregates.txt`.

## Project Layout

```text
CMakeLists.txt
conanfile.txt
config/example.json
output/
src/
tests/
```

Main components planned:

- `config`: reads the JSON configuration file.
- `trade_parser`: converts Binance raw or combined `@trade` payloads into internal trade events.
- `aggregator`: owns exchange-time tumbling-window state and closed-window serialization.
- `binance_client`: planned Boost.Beast/Asio WebSocket boundary for Binance Spot streams.
- `main`: command-line validation, config loading, service lifetime, and signal handling.

## Threading Model

Planned model: a single Boost.Asio event loop handles WebSocket reads, a periodic flush timer, and SIGINT/SIGTERM shutdown. Aggregation stays on that same thread, so no mutexes should be needed for the core path.

## Data Flow

1. Read one config file path from `argv[1]`.
2. Subscribe to `<symbol>@trade` streams for every configured symbol.
3. Parse each Binance trade payload robustly; malformed payloads are logged and skipped.
4. Aggregate by `(symbol, window_start_ms)` using exchange timestamp `T`.
5. Every `flush_interval_ms` of wall-clock time, append all closed windows ordered by `(window_start, symbol)`.
6. On SIGINT/SIGTERM, flush closed windows, close the connection and output file, and exit with code 0.

## Known Limitations

- WebSocket connection, TLS handshake, subscription handling, periodic flushing, and graceful shutdown are not implemented yet.
- Aggregation currently has only a minimal class skeleton.
- Mandatory scenario tests from the PDF are not complete yet.
- Reconnection and connection-loss recovery are intentionally out of scope for the submitted code, but the approach should be documented before the interview.
