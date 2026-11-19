add_library(e3_queue src/placeholder.cpp)
target_include_directories(e3_queue PUBLIC include)
target_link_libraries(e3_queue PUBLIC Threads::Threads)
