#include <gtest/gtest.h>
#include <cstring>
namespace agri_e4 { const char* engine_name(); }

// Stage 1 smoke test: all four engine libraries build and link together.
TEST(Skeleton, AllEnginesLink) {
  EXPECT_STREQ(agri_e4::engine_name(), "e4_web");
}
