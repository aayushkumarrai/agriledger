include(GoogleTest)

add_executable(unit_tests
  unit/smoke_test.cpp
  unit/e1_allocation_test.cpp
  unit/e1_hashing_test.cpp
  unit/e1_invariants_test.cpp)
target_link_libraries(unit_tests PRIVATE e1_audit e2_sale e3_queue e4_web GTest::gtest_main)
gtest_discover_tests(unit_tests)

# Tests that need the real PostgreSQL container: scripts/test.sh creates the database and sets
# AGRI_TEST_CONNINFO and AGRI_TEST_SUPER_CONNINFO. Without them these tests FAIL (they are never skipped).
add_executable(integration_tests integration/e2_test.cpp)
target_link_libraries(integration_tests PRIVATE e2_sale GTest::gtest_main)
gtest_discover_tests(integration_tests PROPERTIES TIMEOUT 300)

# No floating point in engines/, no literal UI strings in web/ (spec §10 "Static")
add_test(NAME static_checks COMMAND ${CMAKE_SOURCE_DIR}/scripts/static_checks.sh)
