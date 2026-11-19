// E1: canonical serialisation, SHA-256 entry hashes and chain verification (spec §5.3, master §8.4).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace e1 {

using i64 = std::int64_t;

extern const char* const kZeroHash;   // 64 zeros: previous_hash of the first entry of a pool

struct InventoryRow { std::string movement_type; i64 delta_g; i64 resulting_stock_g; };
struct OwnershipRow { i64 seller_id; i64 delta_g; i64 resulting_balance_g; };
struct FinancialRow { std::string entry_type; std::optional<i64> seller_id; i64 amount_paise; };
struct PoolChangeRow { std::string field; std::string old_value; std::string new_value; i64 changed_by; };

struct EventRows {
    std::vector<InventoryRow> inventory;       // insertion order
    std::vector<OwnershipRow> ownership;       // any order; serialised sorted by seller_id
    std::vector<FinancialRow> financial;       // any order; SALE_GROSS first, then payouts by seller_id
    std::optional<PoolChangeRow> change;       // PRICE_CHANGED / STATUS_CHANGED events only
};

struct EntryInput {
    i64 pool_id;
    i64 sequence_no;
    i64 event_id;
    std::string event_type;        // POOL_CREATED CONTRIBUTION SALE PRICE_CHANGED STATUS_CHANGED
    std::string created_at_utc;    // "2026-11-17T11:12:00.000000Z" (exactly 6 fractional digits)
    EventRows rows;
    std::string previous_hash;     // 64 lowercase hex characters
};

// Lowercase hex SHA-256 of `data` (OpenSSL EVP). Throws std::runtime_error if OpenSSL fails.
std::string sha256_hex(const std::string& data);

// The `canonical_rows` text of spec §5.3. Throws std::invalid_argument if a text value contains , ; or |.
std::string canonical_rows(const EventRows& rows);

// entry_hash = sha256_hex("pool_id|sequence_no|event_id|event_type|created_at_utc|canonical_rows|previous_hash").
// Throws std::invalid_argument for an unknown event type, a malformed timestamp or previous_hash, or bad text.
std::string compute_entry_hash(const EntryInput& in);

struct ChainEntry { EntryInput input; std::string entry_hash; };

enum class BreakReason { Gap, LinkBroken, HashMismatch };
const char* to_string(BreakReason r);   // "GAP", "LINK_BROKEN", "HASH_MISMATCH"

struct ChainBreak { i64 sequence_no; std::string event_type; BreakReason reason; };
struct ChainResult { bool ok; std::size_t checked; std::optional<ChainBreak> first_broken; };

// Walks one pool's chain, entries ordered as stored. Per entry, in this order: GAP (sequence is not 1,2,3...),
// LINK_BROKEN (previous_hash is not the previous entry's hash, or not the zero hash for entry 1),
// HASH_MISMATCH (recomputed hash differs, or the data cannot be serialised). Reports the first broken entry.
ChainResult verify_chain(const std::vector<ChainEntry>& entries);

}  // namespace e1
