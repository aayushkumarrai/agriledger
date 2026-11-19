# E3 is header-only and knows nothing about SQL: it queues jobs per key (the pool id), runs them on worker threads,
# retries transient results and reports BUSY. e3/e2_adapter.hpp is optional glue for targets that also link e2_sale.
add_library(e3_queue INTERFACE)
target_include_directories(e3_queue INTERFACE include)
target_link_libraries(e3_queue INTERFACE Threads::Threads)
