add_library(e4_web src/placeholder.cpp)
target_include_directories(e4_web PUBLIC include)
target_link_libraries(e4_web PUBLIC e3_queue e2_sale e1_audit nlohmann_json::nlohmann_json PkgConfig::SODIUM)

add_executable(agriledger_server src/main.cpp)
target_link_libraries(agriledger_server PRIVATE e4_web)
