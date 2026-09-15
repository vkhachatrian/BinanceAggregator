cmake_minimum_required(VERSION 3.20)

project(binance_aggregator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BINANCE_AGGREGATOR_BUILD_TESTS "Build unit tests" ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(binance_aggregator_core
    src/aggregator.cpp
    src/config.cpp
    src/trade_parser.cpp
)

target_include_directories(binance_aggregator_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(binance_aggregator_core
    PUBLIC
        Boost::boost
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

target_compile_features(binance_aggregator_core PUBLIC cxx_std_17)

add_executable(binance_aggregator
    src/binance_client.cpp
    src/main.cpp
)

target_link_libraries(binance_aggregator
    PRIVATE
        binance_aggregator_core
        Boost::system
        OpenSSL::Crypto
        OpenSSL::SSL
)

include(CTest)

if(BUILD_TESTING AND BINANCE_AGGREGATOR_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)

    add_executable(binance_aggregator_tests
        tests/aggregator_tests.cpp
        tests/trade_parser_tests.cpp
    )

    target_link_libraries(binance_aggregator_tests
        PRIVATE
            binance_aggregator_core
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(binance_aggregator_tests)
endif()
