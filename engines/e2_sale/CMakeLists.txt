add_library(e2_sale src/placeholder.cpp)
target_include_directories(e2_sale PUBLIC include)
target_link_libraries(e2_sale PUBLIC e1_audit PkgConfig::PQXX)
