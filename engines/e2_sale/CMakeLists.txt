add_library(e2_sale src/common.cpp src/engine.cpp src/integrity.cpp src/views.cpp)
target_include_directories(e2_sale PUBLIC include)
target_link_libraries(e2_sale PUBLIC e1_audit PkgConfig::PQXX nlohmann_json::nlohmann_json Threads::Threads)
