#include "e1/allocation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "e1/wide_int.hpp"

namespace e1 {
namespace {
struct Work { i64 id; i64 base; u128 rem; };
}  // namespace

std::vector<Share> allocate_largest_remainder(i64 total, std::vector<Weight> weights) {
    if (total < 0) throw std::invalid_argument("allocate: total must be >= 0");
    if (weights.empty()) throw std::invalid_argument("allocate: no weights");
    std::sort(weights.begin(), weights.end(), [](const Weight& a, const Weight& b) { return a.id < b.id; });
    u128 sum = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i].weight < 0) throw std::invalid_argument("allocate: negative weight");
        if (i > 0 && weights[i].id == weights[i - 1].id) throw std::invalid_argument("allocate: duplicate id");
        sum += static_cast<u128>(weights[i].weight);
    }
    if (sum == 0) throw std::invalid_argument("allocate: weights sum to zero");

    std::vector<Work> work;
    work.reserve(weights.size());
    i64 assigned = 0;
    for (const Weight& w : weights) {
        const u128 product = static_cast<u128>(total) * static_cast<u128>(w.weight);
        const i64 base = static_cast<i64>(product / sum);   // <= total, so it fits
        work.push_back({w.id, base, product % sum});
        assigned += base;
    }
    i64 leftover = total - assigned;   // 0 <= leftover < number of ids
    // Largest remainder first; ties go to the lowest id (work is already sorted by id, and stable_sort keeps that).
    std::vector<std::size_t> order(work.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return work[a].rem > work[b].rem; });
    for (std::size_t k = 0; k < order.size() && leftover > 0; ++k, --leftover) work[order[k]].base += 1;

    std::vector<Share> out;
    out.reserve(work.size());
    for (const Work& w : work) out.push_back({w.id, w.base});
    return out;
}

i64 gross_paise(i64 quantity_g, i64 price_paise_per_kg) {
    if (quantity_g <= 0) throw std::invalid_argument("gross: quantity must be > 0");
    if (price_paise_per_kg <= 0) throw std::invalid_argument("gross: price must be > 0");
    const u128 scaled = static_cast<u128>(quantity_g) * static_cast<u128>(price_paise_per_kg) + 500;  // half-up
    const u128 gross = scaled / 1000;
    if (gross > static_cast<u128>(std::numeric_limits<i64>::max())) throw std::overflow_error("gross: overflow");
    return static_cast<i64>(gross);
}

SaleSplit compute_sale(i64 quantity_g, i64 price_paise_per_kg, const std::vector<Weight>& balances) {
    if (quantity_g <= 0) throw std::invalid_argument("sale: quantity must be > 0");
    u128 stock = 0;
    for (const Weight& b : balances) {
        if (b.weight < 0) throw std::invalid_argument("sale: negative balance");
        stock += static_cast<u128>(b.weight);
    }
    if (static_cast<u128>(quantity_g) > stock) throw std::invalid_argument("sale: quantity exceeds stock");

    SaleSplit split;
    split.gross_paise = gross_paise(quantity_g, price_paise_per_kg);
    split.alloc_g = allocate_largest_remainder(quantity_g, balances);
    std::vector<Weight> by_alloc;
    by_alloc.reserve(split.alloc_g.size());
    for (const Share& s : split.alloc_g) by_alloc.push_back({s.id, s.amount});
    split.payout_paise = allocate_largest_remainder(split.gross_paise, by_alloc);   // same routine (BR-05)
    return split;
}

}  // namespace e1
