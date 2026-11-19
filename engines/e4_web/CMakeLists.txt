# Crow (REST + WebSocket) is fetched at a pinned release. It needs standalone Asio: the system package libasio-dev is
# used when present (spec §3); otherwise a pinned Asio release is fetched, so the build also works on a machine where
# apt could not be run. Neither project's own CMake is used: both are header-only and added as plain include paths.
include(FetchContent)
find_path(AGRI_ASIO_INCLUDE asio.hpp)
if(AGRI_ASIO_INCLUDE)
  message(STATUS "Asio: using the system package at ${AGRI_ASIO_INCLUDE}")
else()
  message(STATUS "Asio: libasio-dev not found, fetching pinned asio-1-38-2")
  FetchContent_Declare(asio GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git GIT_TAG asio-1-38-2
                       GIT_SHALLOW TRUE SOURCE_SUBDIR no-cmake-here)
  FetchContent_MakeAvailable(asio)
  set(AGRI_ASIO_INCLUDE ${asio_SOURCE_DIR}/asio/include)
endif()
FetchContent_Declare(crow GIT_REPOSITORY https://github.com/CrowCpp/Crow.git GIT_TAG v1.3.5
                     GIT_SHALLOW TRUE SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(crow)

add_library(crow_headers INTERFACE)
target_include_directories(crow_headers SYSTEM INTERFACE ${crow_SOURCE_DIR}/include ${AGRI_ASIO_INCLUDE})
target_compile_definitions(crow_headers INTERFACE ASIO_STANDALONE)
target_link_libraries(crow_headers INTERFACE Threads::Threads)

add_library(e4_web src/server.cpp src/live.cpp src/photos.cpp src/auth.cpp)
target_include_directories(e4_web PUBLIC include)
target_link_libraries(e4_web PUBLIC e3_queue e2_sale e1_audit nlohmann_json::nlohmann_json PkgConfig::SODIUM crow_headers)

add_executable(agriledger_server src/main.cpp)
target_link_libraries(agriledger_server PRIVATE e4_web)
