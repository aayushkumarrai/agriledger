// E1 allocation tests. Reference: master §12 worked example and rounding example.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "e1/allocation.hpp"

using e1::i64;
using e1::Share;
using e1::Weight;

namespace {
std::vector<i64> amounts(const std::vector<Share>& v) {
    std::vector<i64> out;
    for (const Share& s : v) out.push_back(s.amount);
    return out;
}
i64 total_of(const std::vector<Share>& v) {
    i64 t = 0;
    for (const Share& s : v) t += s.amount;
    return t;
}
}  // namespace

// Master §12: wheat-style pool 600/800/600 kg at 2,500 paise/kg, sale of 500 kg.
TEST(E1Allocation, WorkedExample) {
    const std::vector<Weight> balances = {{2, 600000}, {3, 800000}, {4, 600000}};
    const e1::SaleSplit s = e1::compute_sale(500000, 2500, balances);
    EXPECT_EQ(s.gross_paise, 1250000);
    EXPECT_EQ(amounts(s.alloc_g), (std::vector<i64>{150000, 200000, 150000}));
    EXPECT_EQ(amounts(s.payout_paise), (std::vector<i64>{375000, 500000, 375000}));
    EXPECT_EQ(s.alloc_g[0].id, 2);
    EXPECT_EQ(s.alloc_g[2].id, 4);
}

// Master §12 rounding example: three farmers with 1,000 g each, sale of 100 g at 2,500 paise/kg.
TEST(E1Allocation, RoundingExample) {
    const std::vector<Weight> balances = {{1, 1000}, {2, 1000}, {3, 1000}};
    const e1::SaleSplit s = e1::compute_sale(100, 2500, balances);
    EXPECT_EQ(amounts(s.alloc_g), (std::vector<i64>{34, 33, 33}));    // tie goes to the lowest id
    EXPECT_EQ(s.gross_paise, 250);
    EXPECT_EQ(amounts(s.payout_paise), (std::vector<i64>{85, 83, 82}));
    EXPECT_EQ(total_of(s.payout_paise), 250);
}

TEST(E1Allocation, TiesGoToLowestIdWhateverTheInputOrder) {
    const std::vector<Share> a = e1::allocate_largest_remainder(1, {{9, 1}, {3, 1}, {5, 1}});
    EXPECT_EQ(a[0].id, 3);
    EXPECT_EQ(amounts(a), (std::vector<i64>{1, 0, 0}));
    const std::vector<Share> b = e1::allocate_largest_remainder(2, {{9, 1}, {3, 1}, {5, 1}});
    EXPECT_EQ(amounts(b), (std::vector<i64>{1, 1, 0}));   // ids 3 and 5
}

TEST(E1Allocation, LargestRemainderBeatsLowestIdWhenRemaindersDiffer) {
    // 10 split 2:3:3 -> exact shares 2.5, 3.75, 3.75; floors 2,3,3 (sum 8), leftover 2 goes to the two largest
    // remainders (.75 and .75, ids 2 and 3), not to id 1 (.5) even though it has the lowest id.
    const std::vector<Share> r = e1::allocate_largest_remainder(10, {{1, 2}, {2, 3}, {3, 3}});
    EXPECT_EQ(amounts(r), (std::vector<i64>{2, 4, 4}));
}

TEST(E1Allocation, ZeroWeightFarmerGetsNothing) {
    const std::vector<Share> r = e1::allocate_largest_remainder(7, {{1, 0}, {2, 3}, {3, 4}});
    EXPECT_EQ(amounts(r), (std::vector<i64>{0, 3, 4}));
}

TEST(E1Allocation, ExactSumsOverManyPseudoRandomCases) {
    std::uint64_t seed = 12345;   // deterministic integer generator (no floating point)
    auto next = [&seed](std::uint64_t mod) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return (seed >> 33) % mod;
    };
    for (int iter = 0; iter < 2000; ++iter) {
        const int n = 1 + static_cast<int>(next(8));
        std::vector<Weight> w;
        i64 stock = 0;
        for (int i = 0; i < n; ++i) {
            const i64 x = static_cast<i64>(next(5000000));
            w.push_back({100 + i, x});
            stock += x;
        }
        if (stock == 0) continue;
        const i64 q = 1 + static_cast<i64>(next(static_cast<std::uint64_t>(stock)));
        const i64 price = 1 + static_cast<i64>(next(100000));
        const e1::SaleSplit s = e1::compute_sale(q, price, w);
        ASSERT_EQ(total_of(s.alloc_g), q);
        ASSERT_EQ(total_of(s.payout_paise), s.gross_paise);
        for (std::size_t i = 0; i < w.size(); ++i) ASSERT_LE(s.alloc_g[i].amount, w[i].weight);   // never more than owned
    }
}

TEST(E1Allocation, GrossRoundsHalfUpOnce) {
    EXPECT_EQ(e1::gross_paise(1, 500), 1);      // 0.5 paise rounds up
    EXPECT_EQ(e1::gross_paise(1, 499), 0);      // 0.499 rounds down
    EXPECT_EQ(e1::gross_paise(1000, 2500), 2500);
    EXPECT_EQ(e1::gross_paise(500000, 2500), 1250000);
    EXPECT_EQ(e1::gross_paise(3, 1500), 5);     // 4.5 -> 5
}

TEST(E1Allocation, HugeValuesUseWideIntermediates) {
    const i64 big = 4000000000000000000LL;   // 4e18 g: quantity * weight is about 1.6e37, far above int64
    const std::vector<Share> r = e1::allocate_largest_remainder(big, {{1, big}, {2, big}});
    EXPECT_EQ(total_of(r), big);
    EXPECT_EQ(amounts(r), (std::vector<i64>{big / 2, big / 2}));
    EXPECT_THROW(e1::gross_paise(INT64_MAX, INT64_MAX), std::overflow_error);
}

TEST(E1Allocation, RejectsInvalidInput) {
    EXPECT_THROW(e1::allocate_largest_remainder(-1, {{1, 1}}), std::invalid_argument);
    EXPECT_THROW(e1::allocate_largest_remainder(5, {}), std::invalid_argument);
    EXPECT_THROW(e1::allocate_largest_remainder(5, {{1, 0}, {2, 0}}), std::invalid_argument);
    EXPECT_THROW(e1::allocate_largest_remainder(5, {{1, -1}, {2, 3}}), std::invalid_argument);
    EXPECT_THROW(e1::allocate_largest_remainder(5, {{1, 1}, {1, 2}}), std::invalid_argument);
    EXPECT_THROW(e1::gross_paise(0, 100), std::invalid_argument);
    EXPECT_THROW(e1::gross_paise(100, 0), std::invalid_argument);
    EXPECT_THROW(e1::compute_sale(0, 2500, {{1, 10}}), std::invalid_argument);          // BR-06: Q <= 0
    EXPECT_THROW(e1::compute_sale(11, 2500, {{1, 10}}), std::invalid_argument);         // BR-06: Q > stock
}
