#include <gtest/gtest.h>
#include <cstring>
namespace agri_e1 { const char* engine_name(); }
namespace agri_e2 { const char* engine_name(); }
namespace agri_e3 { const char* engine_name(); }
namespace agri_e4 { const char* engine_name(); }

// Stage 1 smoke test: all four engine libraries build and link together.
TEST(Skeleton, AllEnginesLink) {
  EXPECT_STREQ(agri_e1::engine_name(), "e1_audit");
  EXPECT_STREQ(agri_e2::engine_name(), "e2_sale");
  EXPECT_STREQ(agri_e3::engine_name(), "e3_queue");
  EXPECT_STREQ(agri_e4::engine_name(), "e4_web");
}
