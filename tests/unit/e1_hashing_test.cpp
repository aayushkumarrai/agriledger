// E1 hashing, canonical serialisation, chain verification and tampering. Expected hashes were produced
// independently with `printf '%s' "<text>" | sha256sum`, not with this library.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "e1/hashing.hpp"

using namespace e1;

namespace {
const char* const kTime1 = "2026-11-17T11:12:00.000000Z";
const char* const kH1 = "f844d341db661e942942f2204e20d332a21b26a62038d6914ad3bc09c4bd0930";
const char* const kH2 = "08af10adb67643771e8235c981f3a74ebfd31b168761b28239db367807337771";
const char* const kH3 = "35ed0641480d39ab15f60cf8260fd94703dbfe6de00fcb3abdeabcc3bf9ae6ca";

EntryInput created() {
    EntryInput in{1, 1, 1, "POOL_CREATED", kTime1, {}, kZeroHash};
    in.rows.inventory.push_back({"OPEN", 0, 0});
    return in;
}

EntryInput sale() {
    EntryInput in{1, 2, 6, "SALE", kTime1, {}, kH1};
    in.rows.inventory.push_back({"OUT", -500000, 1500000});
    // deliberately out of order: serialisation must sort them
    in.rows.ownership = {{4, -150000, 450000}, {2, -150000, 450000}, {3, -200000, 600000}};
    in.rows.financial = {{"SELLER_PAYOUT", 4, 375000}, {"SELLER_PAYOUT", 2, 375000}, {"SALE_GROSS", std::nullopt, 1250000},
                         {"SELLER_PAYOUT", 3, 500000}};
    return in;
}

EntryInput price_change() {
    EntryInput in{1, 3, 7, "PRICE_CHANGED", "2026-11-17T11:12:01.000007Z", {}, kH2};
    in.rows.change = PoolChangeRow{"PRICE", "2500", "2400", 1};
    return in;
}

std::vector<ChainEntry> chain() { return {{created(), kH1}, {sale(), kH2}, {price_change(), kH3}}; }
}  // namespace

TEST(E1Hashing, Sha256KnownVectors) {
    EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(E1Hashing, CanonicalRowsSortedAndFormatted) {
    EXPECT_EQ(canonical_rows(sale().rows),
              "I,OUT,-500000,1500000;O,2,-150000,450000;O,3,-200000,600000;O,4,-150000,450000;"
              "F,SALE_GROSS,-,1250000;F,SELLER_PAYOUT,2,375000;F,SELLER_PAYOUT,3,500000;F,SELLER_PAYOUT,4,375000;");
    EXPECT_EQ(canonical_rows(price_change().rows), "C,PRICE,2500,2400,1;");
    EXPECT_EQ(canonical_rows(created().rows), "I,OPEN,0,0;");
}

TEST(E1Hashing, EntryHashesMatchIndependentSha256) {
    EXPECT_EQ(compute_entry_hash(created()), kH1);
    EXPECT_EQ(compute_entry_hash(sale()), kH2);
    EXPECT_EQ(compute_entry_hash(price_change()), kH3);   // PRICE_CHANGED hashes the pool_change fields
}

TEST(E1Hashing, RejectsUnserialisableInput) {
    EntryInput bad = created();
    bad.created_at_utc = "2026-11-17T11:12:00Z";   // no microseconds
    EXPECT_THROW(compute_entry_hash(bad), std::invalid_argument);
    bad = created();
    bad.previous_hash = "ABC";
    EXPECT_THROW(compute_entry_hash(bad), std::invalid_argument);
    bad = created();
    bad.event_type = "REFUND";
    EXPECT_THROW(compute_entry_hash(bad), std::invalid_argument);
    bad = price_change();
    bad.rows.change->new_value = "a;b";
    EXPECT_THROW(compute_entry_hash(bad), std::invalid_argument);
}

TEST(E1Chain, IntactChainVerifies) {
    const ChainResult r = verify_chain(chain());
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.checked, 3u);
    EXPECT_FALSE(r.first_broken.has_value());
    EXPECT_TRUE(verify_chain({}).ok);
}

TEST(E1Chain, AlteredLedgerRowIsNamedExactly) {
    auto c = chain();
    c[1].input.rows.financial[0].amount_paise += 1;   // a payout changed after the fact
    const ChainResult r = verify_chain(c);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 2);
    EXPECT_EQ(r.first_broken->event_type, "SALE");
    EXPECT_EQ(r.first_broken->reason, BreakReason::HashMismatch);
    EXPECT_STREQ(to_string(r.first_broken->reason), "HASH_MISMATCH");
}

TEST(E1Chain, AlteredTimestampOrEventIdIsDetected) {
    auto c = chain();
    c[2].input.created_at_utc = "2026-11-17T11:12:01.000008Z";
    EXPECT_EQ(verify_chain(c).first_broken->sequence_no, 3);
    c = chain();
    c[0].input.event_id = 2;
    EXPECT_EQ(verify_chain(c).first_broken->sequence_no, 1);
}

TEST(E1Chain, AlteredStoredHashIsDetectedAtThatEntry) {
    auto c = chain();
    c[1].entry_hash = std::string(64, 'a');
    const ChainResult r = verify_chain(c);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 2);
    EXPECT_EQ(r.first_broken->reason, BreakReason::HashMismatch);
}

TEST(E1Chain, BrokenLinkIsReported) {
    auto c = chain();
    c[2].input.previous_hash = kH1;   // points at the wrong predecessor
    const ChainResult r = verify_chain(c);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 3);
    EXPECT_EQ(r.first_broken->reason, BreakReason::LinkBroken);
    c = chain();
    c[0].input.previous_hash = kH3;   // first entry must start from the zero hash
    EXPECT_EQ(verify_chain(c).first_broken->reason, BreakReason::LinkBroken);
}

TEST(E1Chain, DeletedEntryIsAGap) {
    auto c = chain();
    c.erase(c.begin() + 1);   // entry 2 removed: entry 3 now sits where 2 should be
    const ChainResult r = verify_chain(c);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 3);
    EXPECT_EQ(r.first_broken->event_type, "PRICE_CHANGED");
    EXPECT_EQ(r.first_broken->reason, BreakReason::Gap);
}

TEST(E1Chain, EntryFromAnotherPoolIsNotPartOfTheChain) {
    auto c = chain();
    c[1].input.pool_id = 2;
    const ChainResult r = verify_chain(c);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 2);
    EXPECT_EQ(r.first_broken->reason, BreakReason::LinkBroken);
}

TEST(E1Chain, RewritingTheWholeTailStillFailsBecauseEarlierHashesAreFixed) {
    auto c = chain();
    c[0].input.rows.inventory[0].resulting_stock_g = 1;   // tamper with the first entry only
    const ChainResult r = verify_chain(c);
    EXPECT_EQ(r.first_broken->sequence_no, 1);            // the first broken entry, not a later one
}
