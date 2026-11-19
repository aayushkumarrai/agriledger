#include "e1/hashing.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace e1 {

const char* const kZeroHash = "0000000000000000000000000000000000000000000000000000000000000000";

namespace {

bool is_hash(const std::string& s) {
    if (s.size() != 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// "YYYY-MM-DDTHH:MM:SS.ffffffZ": exactly 27 characters.
bool is_timestamp(const std::string& s) {
    if (s.size() != 27) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        switch (i) {
            case 4: case 7: if (c != '-') return false; break;
            case 10: if (c != 'T') return false; break;
            case 13: case 16: if (c != ':') return false; break;
            case 19: if (c != '.') return false; break;
            case 26: if (c != 'Z') return false; break;
            default: if (c < '0' || c > '9') return false;
        }
    }
    return true;
}

bool is_event_type(const std::string& s) {
    return s == "POOL_CREATED" || s == "CONTRIBUTION" || s == "SALE" || s == "PRICE_CHANGED" || s == "STATUS_CHANGED";
}

const std::string& checked_text(const std::string& s) {
    if (s.find_first_of(",;|") != std::string::npos)
        throw std::invalid_argument("text value contains a reserved character (, ; |)");
    return s;
}

}  // namespace

std::string sha256_hex(const std::string& data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
    unsigned int len = 0;
    if (EVP_Digest(data.data(), data.size(), md.data(), &len, EVP_sha256(), nullptr) != 1 || len != 32)
        throw std::runtime_error("OpenSSL SHA-256 failed");
    static const char* const hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(hex[md[i] >> 4]);
        out.push_back(hex[md[i] & 0x0F]);
    }
    return out;
}

std::string canonical_rows(const EventRows& rows) {
    std::string out;
    for (const InventoryRow& r : rows.inventory)
        out += "I," + checked_text(r.movement_type) + "," + std::to_string(r.delta_g) + "," +
               std::to_string(r.resulting_stock_g) + ";";

    std::vector<OwnershipRow> own = rows.ownership;
    std::stable_sort(own.begin(), own.end(), [](const OwnershipRow& a, const OwnershipRow& b) { return a.seller_id < b.seller_id; });
    for (const OwnershipRow& r : own)
        out += "O," + std::to_string(r.seller_id) + "," + std::to_string(r.delta_g) + "," +
               std::to_string(r.resulting_balance_g) + ";";

    std::vector<FinancialRow> fin = rows.financial;
    std::stable_sort(fin.begin(), fin.end(), [](const FinancialRow& a, const FinancialRow& b) {
        const bool a_gross = a.entry_type == "SALE_GROSS", b_gross = b.entry_type == "SALE_GROSS";
        if (a_gross != b_gross) return a_gross;   // SALE_GROSS first
        return a.seller_id.value_or(0) < b.seller_id.value_or(0);
    });
    for (const FinancialRow& r : fin)
        out += "F," + checked_text(r.entry_type) + "," + (r.seller_id ? std::to_string(*r.seller_id) : std::string("-")) +
               "," + std::to_string(r.amount_paise) + ";";

    if (rows.change)
        out += "C," + checked_text(rows.change->field) + "," + checked_text(rows.change->old_value) + "," +
               checked_text(rows.change->new_value) + "," + std::to_string(rows.change->changed_by) + ";";
    return out;
}

std::string compute_entry_hash(const EntryInput& in) {
    if (!is_event_type(in.event_type)) throw std::invalid_argument("unknown event type");
    if (!is_timestamp(in.created_at_utc)) throw std::invalid_argument("created_at_utc must be YYYY-MM-DDTHH:MM:SS.ffffffZ");
    if (!is_hash(in.previous_hash)) throw std::invalid_argument("previous_hash must be 64 lowercase hex characters");
    const std::string text = std::to_string(in.pool_id) + "|" + std::to_string(in.sequence_no) + "|" +
                             std::to_string(in.event_id) + "|" + in.event_type + "|" + in.created_at_utc + "|" +
                             canonical_rows(in.rows) + "|" + in.previous_hash;
    return sha256_hex(text);
}

const char* to_string(BreakReason r) {
    switch (r) {
        case BreakReason::Gap: return "GAP";
        case BreakReason::LinkBroken: return "LINK_BROKEN";
        case BreakReason::HashMismatch: return "HASH_MISMATCH";
    }
    return "HASH_MISMATCH";
}

ChainResult verify_chain(const std::vector<ChainEntry>& entries) {
    std::string expected_previous = kZeroHash;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ChainEntry& e = entries[i];
        auto broken = [&](BreakReason why) {
            return ChainResult{false, i + 1, ChainBreak{e.input.sequence_no, e.input.event_type, why}};
        };
        if (e.input.sequence_no != static_cast<i64>(i) + 1) return broken(BreakReason::Gap);
        if (e.input.pool_id != entries[0].input.pool_id) return broken(BreakReason::LinkBroken);   // one chain = one pool
        if (e.input.previous_hash != expected_previous) return broken(BreakReason::LinkBroken);
        std::string recomputed;
        try {
            recomputed = compute_entry_hash(e.input);
        } catch (const std::invalid_argument&) {
            return broken(BreakReason::HashMismatch);   // altered data that can no longer be serialised
        }
        if (recomputed != e.entry_hash) return broken(BreakReason::HashMismatch);
        expected_previous = e.entry_hash;
    }
    return ChainResult{true, entries.size(), std::nullopt};
}

}  // namespace e1
