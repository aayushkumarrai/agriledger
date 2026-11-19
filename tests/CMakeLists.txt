include(GoogleTest)

add_executable(unit_tests unit/smoke_test.cpp)
target_link_libraries(unit_tests PRIVATE e1_audit e2_sale e3_queue e4_web GTest::gtest_main)
gtest_discover_tests(unit_tests)
