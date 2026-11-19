// E2 Atomic Sale Engine: all SQL, transactions, row locks, ledger rows, reconciliation and NOTIFY (master §7.3).
// Every operation is one transaction that takes the pool row lock first, writes its event and ledger rows, seals an
// E1 hash-chain entry, runs INV-1..5, sends NOTIFY and commits. Any failure rolls the whole thing back.
// All SQL uses bound parameters.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "e1/invariants.hpp"

namespace e2 {

using i64 = std::int64_t;

struct Config {
    std::string conninfo;          // libpq connection string of the application LOGIN user
    std::size_t pool_size = 8;     // maximum simultaneous connections
    int lock_timeout_ms = 3000;    // how long a statement waits for a row lock before the attempt is transient
};

// ---- errors ----------------------------------------------------------------------------------------------------

// A request that cannot be done (maps to HTTP 400/404/409 in E4). Codes: NOT_FOUND, INVALID, POOL_NOT_OPEN,
// PHOTO_REQUIRED, DUPLICATE_USERNAME, NO_CHANGE, INVALID_TRANSITION, REQUEST_ID_CONFLICT.
class DomainError : public std::runtime_error {
public:
    DomainError(std::string code, const std::string& message) : std::runtime_error(message), code_(std::move(code)) {}
    const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

// Lock timeout, deadlock or serialization failure: nothing was changed; the caller may retry (E3 policy).
class TransientError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// INV-1..5 failed before commit; the transaction was rolled back.
class ReconciliationFailed : public std::runtime_error {
public:
    explicit ReconciliationFailed(std::vector<e1::InvariantFailure> f);
    const std::vector<e1::InvariantFailure>& failures() const noexcept { return failures_; }
private:
    std::vector<e1::InvariantFailure> failures_;
};

// ---- sale ------------------------------------------------------------------------------------------------------

struct SaleRequest {
    std::string request_id;                 // UUID made by the browser when the confirmation opens
    i64 pool_id = 0;
    i64 quantity_g = 0;
    i64 confirmed_price_paise_per_kg = 0;   // the price the buyer saw and confirmed
    i64 buyer_id = 0;
    i64 actor_user_id = 0;                  // the buyer, or the Manager placing the sale for the buyer
};

struct PayoutRow { i64 seller_id; i64 alloc_g; i64 paise; };

struct Receipt {
    i64 sale_id = 0;
    std::string receipt_no;                 // "R-0091"
    i64 pool_id = 0;
    i64 quantity_g = 0;
    i64 unit_price_paise_per_kg = 0;
    i64 gross_paise = 0;
    std::string created_at;                 // "2026-11-17T11:12:00Z"
    std::string storage_location;
    i64 stock_after_g = 0;
    std::vector<PayoutRow> payouts;         // E4 sends these to the Manager only, never to a Buyer
    bool duplicate = false;                 // true when this is the stored result of an earlier identical request
};

enum class Reason { OutOfStock, InsufficientStock, PriceChanged, BelowMinOrder, PoolNotOpen };
const char* to_string(Reason r);            // OUT_OF_STOCK INSUFFICIENT_STOCK PRICE_CHANGED BELOW_MIN_ORDER POOL_NOT_OPEN

struct SaleOutcome {
    // Transient: E2 changed nothing and a retry may work. Busy: set by E3 only, after its retries or its queue timeout ran out.
    enum class Status { Accepted, Rejected, Transient, Busy } status = Status::Rejected;
    Receipt receipt;                        // when Accepted
    Reason reason = Reason::PoolNotOpen;    // when Rejected
    i64 stock_g = 0;                        // INSUFFICIENT_STOCK: remaining stock
    i64 current_price_paise_per_kg = 0;     // PRICE_CHANGED
    i64 min_order_g = 0;                    // BELOW_MIN_ORDER
};

// Points inside the sale transaction, for failure-injection tests (T-FR-09). Production code passes no hook.
enum class SaleStep {
    PoolLocked, Validated, EventInserted, SaleInserted, PayoutsInserted, BalancesUpdated, StockUpdated,
    LedgersInserted, AuditSealed, Reconciled, Notified
};
using SaleStepHook = std::function<void(SaleStep)>;   // may throw to simulate a failure after that step

// ---- other operations ------------------------------------------------------------------------------------------

struct NewUser {
    std::string name;
    std::string username;                   // 3..50 characters: a-z 0-9 . _ -
    std::string type;                       // MANAGER, FARMER or BUYER
    std::string password_hash;              // argon2id string made by E4; E2 never sees a password
};

struct NewPool {
    i64 commodity_id = 0;
    std::string variety;
    int range_no = 1;                       // 1..3
    std::optional<std::string> packaging;
    std::string storage_location;
    i64 min_order_g = 0;
    i64 price_paise_per_kg = 0;
};

struct ContributionResult { i64 event_id; i64 stock_after_g; i64 seller_balance_g; };

struct ReconciliationStatus {
    bool ok = true;
    std::vector<e1::InvariantFailure> failures;
};

struct IntegrityBreak {
    i64 sequence_no = 0;
    std::string event_type;
    std::string reason;                     // HASH_MISMATCH, LINK_BROKEN, GAP or INVARIANT
};

struct IntegrityResult {
    bool ok = true;
    std::size_t checked_entries = 0;
    std::optional<IntegrityBreak> first_broken;
};

class Engine {
public:
    explicit Engine(Config config);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Accounts. must_change_password stays true (schema default). No event, no ledger.
    i64 create_user(const NewUser& user);

    // Pools: status 'draft', stock 0, display unit 'quintal'. Event POOL_CREATED.
    i64 create_pool(const NewPool& pool, i64 actor_user_id);
    // Allowed while the pool is draft or open; a closed pool is refused with POOL_NOT_OPEN. Event CONTRIBUTION.
    ContributionResult record_contribution(i64 pool_id, i64 seller_id, i64 quantity_g, i64 actor_user_id);
    // Photo row only (append-only table); no event. Returns photo_id.
    i64 add_photo(i64 pool_id, i64 uploaded_by, const std::string& file_path, const std::string& sha256_hex);
    // Future sales only. Event PRICE_CHANGED plus a pool_change row.
    void change_price(i64 pool_id, i64 new_price_paise_per_kg, i64 actor_user_id);
    // draft->open, open->closed, closed->open (opening needs a photo). Event STATUS_CHANGED plus a pool_change row.
    void change_status(i64 pool_id, const std::string& new_status, i64 actor_user_id);

    // The one sale path (spec §5.1). Accepted and Rejected are normal results; Transient means "nothing changed, retry".
    // Throws DomainError for malformed requests, ReconciliationFailed if INV-1..5 fail (rolled back).
    SaleOutcome execute_sale(const SaleRequest& request, const SaleStepHook* test_hook = nullptr);

    // Reads that need the whole ledger history.
    ReconciliationStatus reconciliation(i64 pool_id);
    IntegrityResult integrity_check(i64 pool_id);

    struct Impl;   // opaque: defined inside E2 only

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace e2
