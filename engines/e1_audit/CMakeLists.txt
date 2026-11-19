add_library(e1_audit src/allocation.cpp src/hashing.cpp src/invariants.cpp)
target_include_directories(e1_audit PUBLIC include)
target_link_libraries(e1_audit PUBLIC OpenSSL::Crypto)
