// E1: exact integer allocation (master BR-04, BR-05). No floating point anywhere in this library.
#pragma once
#include <cstdint>
#include <vector>

namespace e1 {

using i64 = std::int64_t;

struct Weight { i64 id; i64 weight; };   // id = seller_id, weight = remaining grams (or allocated grams)
struct Share  { i64 id; i64 amount; };   // result for one id

// Splits `total` among the weights by the largest-remainder method:
//   base_i = floor(total * w_i / sum_w), then +1 to the `leftover` ids with the largest remainder,
//   ties going to the lowest id. The result sums to `total` exactly and is sorted by id ascending.
// Throws std::invalid_argument for: total < 0, no weights, a negative weight, duplicate ids, or sum_w == 0.
std::vector<Share> allocate_largest_remainder(i64 total, std::vector<Weight> weights);

// gross_paise = round_half_up(quantity_g * price_paise_per_kg / 1000), rounded once (BR-05).
// Throws std::invalid_argument if either argument is <= 0, std::overflow_error if the result exceeds int64.
i64 gross_paise(i64 quantity_g, i64 price_paise_per_kg);

struct SaleSplit {
    i64 gross_paise;
    std::vector<Share> alloc_g;        // per seller, sorted by seller_id, sums to quantity_g
    std::vector<Share> payout_paise;   // per seller, sorted by seller_id, sums to gross_paise
};

// One sale: quantity split by ownership, then the gross split by those allocations (same routine).
// Throws std::invalid_argument if quantity_g <= 0, price <= 0, or quantity_g exceeds the sum of balances.
SaleSplit compute_sale(i64 quantity_g, i64 price_paise_per_kg, const std::vector<Weight>& balances);

}  // namespace e1
