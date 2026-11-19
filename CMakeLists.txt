cmake_minimum_required(VERSION 3.20)
project(AgriLedger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)

enable_testing()

# One library per engine (spec §2). e3_queue knows nothing about SQL (it runs a callback); e4 and the test harness wire E3 to E2.
add_subdirectory(engines/e1_audit)   # pure C++, no database, no HTTP
add_subdirectory(engines/e2_sale)    # libpqxx transactions
add_subdirectory(engines/e3_queue)   # per-pool FIFO, workers, retry
add_subdirectory(engines/e4_web)     # REST, WebSocket, auth; also the server executable
add_subdirectory(tests)
