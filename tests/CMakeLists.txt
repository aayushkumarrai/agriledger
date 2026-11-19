include(GoogleTest)

add_executable(unit_tests
  unit/smoke_test.cpp
  unit/e1_allocation_test.cpp
  unit/e1_hashing_test.cpp
  unit/e1_invariants_test.cpp
  unit/e3_queue_test.cpp)
target_link_libraries(unit_tests PRIVATE e1_audit e2_sale e3_queue e4_web GTest::gtest_main)
gtest_discover_tests(unit_tests)

# Tests that need the real PostgreSQL container: scripts/test.sh creates the database and sets
# AGRI_TEST_CONNINFO and AGRI_TEST_SUPER_CONNINFO. Without them these tests FAIL (they are never skipped).
add_executable(integration_tests integration/e2_test.cpp)
target_link_libraries(integration_tests PRIVATE e2_sale GTest::gtest_main)
gtest_discover_tests(integration_tests PROPERTIES TIMEOUT 300)

# Concurrency: 50 buyers x 20 runs through E3 into E2, plus the harness-only unprotected comparison (never in the server).
add_executable(concurrency_tests concurrency/concurrency_test.cpp)
target_link_libraries(concurrency_tests PRIVATE e2_sale e3_queue GTest::gtest_main)
gtest_discover_tests(concurrency_tests PROPERTIES TIMEOUT 600)
add_executable(concurrency_demo concurrency/concurrency_demo.cpp)
target_link_libraries(concurrency_demo PRIVATE e2_sale e3_queue)

# No floating point in engines/, no literal UI strings in web/ (spec §10 "Static")
add_test(NAME static_checks COMMAND ${CMAKE_SOURCE_DIR}/scripts/static_checks.sh)
