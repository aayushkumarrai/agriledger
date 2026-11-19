// E2 against the real database: worked example, every reason code, duplicate request_id, a failure injected after
// each step of a sale, corrupted state, notifications, tampering, and the other operations.
#include <thread>

#include "db_fixture.hpp"

using namespace testdb;
using e2::Reason;
using e2::SaleOutcome;
using e2::SaleStep;

namespace {

// Receives NOTIFY payloads on its own connection.
class Listener : public pqxx::notification_receiver {
public:
    explicit Listener(pqxx::connection& c) : pqxx::notification_receiver(c, "ledger_events"), conn_(c) {}
    void operator()(const std::string& payload, int) override { payloads.push_back(payload); }
    void drain(int wait_ms) { conn_.await_notification(0, wait_ms * 1000L); }
    std::vector<std::string> payloads;
private:
    pqxx::connection& conn_;
};

const SaleStep kAllSteps[] = {SaleStep::PoolLocked, SaleStep::Validated, SaleStep::EventInserted, SaleStep::SaleInserted,
                              SaleStep::PayoutsInserted, SaleStep::BalancesUpdated, SaleStep::StockUpdated,
                              SaleStep::LedgersInserted, SaleStep::AuditSealed, SaleStep::Reconciled, SaleStep::Notified};

}  // namespace

class E2Test : public DbTest {
protected:
    e2::Engine eng{config()};
    World world{eng};
};

// ---- master §12 worked example, through every layer of E2 -------------------------------------------------------

TEST_F(E2Test, WorkedExample) {
    const i64 pool = world.make_worked_example_pool();
    const SaleOutcome o = eng.execute_sale(world.order(pool, 500000));
    ASSERT_EQ(o.status, SaleOutcome::Status::Accepted);
    const e2::Receipt& r = o.receipt;
    EXPECT_FALSE(r.duplicate);
    EXPECT_EQ(r.gross_paise, 1250000);
    EXPECT_EQ(r.quantity_g, 500000);
    EXPECT_EQ(r.unit_price_paise_per_kg, 2500);
    EXPECT_EQ(r.stock_after_g, 1500000);
    EXPECT_EQ(r.receipt_no.substr(0, 2), "R-");
    EXPECT_EQ(r.storage_location, "Test godown, Hubballi");
    ASSERT_EQ(r.payouts.size(), 3u);
    EXPECT_EQ(r.payouts[0].seller_id, world.farmer[0]);
    EXPECT_EQ(r.payouts[0].alloc_g, 150000);
    EXPECT_EQ(r.payouts[0].paise, 375000);
    EXPECT_EQ(r.payouts[1].alloc_g, 200000);
    EXPECT_EQ(r.payouts[1].paise, 500000);
    EXPECT_EQ(r.payouts[2].alloc_g, 150000);
    EXPECT_EQ(r.payouts[2].paise, 375000);

    pqxx::connection admin(super_conninfo());
    pqxx::nontransaction n(admin);
    for (int i = 0; i < 3; ++i) {
        const i64 left = n.exec_params1("SELECT remaining_g FROM pool_seller_balance WHERE pool_id = $1 AND seller_id = $2", pool, world.farmer[i])[0].as<i64>();
        EXPECT_EQ(left, i == 1 ? 600000 : 450000);
    }
    const auto snap = snapshot(pool);
    EXPECT_EQ(snap.at("stock"), 1500000);
    EXPECT_EQ(snap.at("ledger_entry"), 6);   // created, 3 deposits, opened, sale
    EXPECT_EQ(snap.at("financial_ledger"), 4);   // gross + 3 payouts

    EXPECT_TRUE(eng.reconciliation(pool).ok);
    const e2::IntegrityResult ic = eng.integrity_check(pool);
    EXPECT_TRUE(ic.ok);
    EXPECT_EQ(ic.checked_entries, 6u);
}

TEST_F(E2Test, RoundingExampleThroughTheDatabase) {
    const i64 pool = world.make_pool(2500, 1);
    for (int i = 0; i < 3; ++i) eng.record_contribution(pool, world.farmer[i], 1000, world.manager);
    const SaleOutcome o = eng.execute_sale(world.order(pool, 100));
    ASSERT_EQ(o.status, SaleOutcome::Status::Accepted);
    EXPECT_EQ(o.receipt.gross_paise, 250);
    EXPECT_EQ(o.receipt.payouts[0].alloc_g, 34);
    EXPECT_EQ(o.receipt.payouts[1].alloc_g, 33);
    EXPECT_EQ(o.receipt.payouts[2].alloc_g, 33);
    EXPECT_EQ(o.receipt.payouts[0].paise, 85);
    EXPECT_EQ(o.receipt.payouts[1].paise, 83);
    EXPECT_EQ(o.receipt.payouts[2].paise, 82);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

// ---- every reason code ------------------------------------------------------------------------------------------

TEST_F(E2Test, InsufficientStockReturnsRemainingStock) {
    const i64 pool = world.make_worked_example_pool();
    const SaleOutcome o = eng.execute_sale(world.order(pool, 2000001));
    ASSERT_EQ(o.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(o.reason, Reason::InsufficientStock);
    EXPECT_STREQ(e2::to_string(o.reason), "INSUFFICIENT_STOCK");
    EXPECT_EQ(o.stock_g, 2000000);
    EXPECT_EQ(snapshot(pool).at("sale"), 0);
}

TEST_F(E2Test, OutOfStockWhenNothingIsLeft_PoolStaysOpen) {
    const i64 pool = world.make_pool();
    eng.record_contribution(pool, world.farmer[0], 200000, world.manager);
    ASSERT_EQ(eng.execute_sale(world.order(pool, 200000)).status, SaleOutcome::Status::Accepted);
    const SaleOutcome o = eng.execute_sale(world.order(pool, 100000));
    ASSERT_EQ(o.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(o.reason, Reason::OutOfStock);

    pqxx::connection admin(super_conninfo());
    pqxx::nontransaction n(admin);
    EXPECT_EQ(n.exec_params1("SELECT status FROM pool WHERE pool_id = $1", pool)[0].as<std::string>(), "open");   // no auto-close
    EXPECT_EQ(snapshot(pool).at("pool_change"), 1);   // only the draft -> open change

    // a deposit makes it sellable again
    eng.record_contribution(pool, world.farmer[1], 300000, world.manager);
    EXPECT_EQ(eng.execute_sale(world.order(pool, 100000)).status, SaleOutcome::Status::Accepted);
}

TEST_F(E2Test, PriceChangedReturnsCurrentPrice) {
    const i64 pool = world.make_worked_example_pool();
    eng.change_price(pool, 2400, world.manager);
    const SaleOutcome o = eng.execute_sale(world.order(pool, 500000, 2500));
    ASSERT_EQ(o.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(o.reason, Reason::PriceChanged);
    EXPECT_EQ(o.current_price_paise_per_kg, 2400);
    const SaleOutcome again = eng.execute_sale(world.order(pool, 500000, 2400));   // "Review and confirm again"
    ASSERT_EQ(again.status, SaleOutcome::Status::Accepted);
    EXPECT_EQ(again.receipt.gross_paise, 1200000);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

TEST_F(E2Test, BelowMinOrderUnlessItTakesTheWholeRemainder) {
    const i64 pool = world.make_pool(2500, 100000);
    eng.record_contribution(pool, world.farmer[0], 150000, world.manager);
    const SaleOutcome low = eng.execute_sale(world.order(pool, 50000));
    ASSERT_EQ(low.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(low.reason, Reason::BelowMinOrder);
    EXPECT_EQ(low.min_order_g, 100000);
    ASSERT_EQ(eng.execute_sale(world.order(pool, 100000)).status, SaleOutcome::Status::Accepted);
    // 50,000 g is left, below the minimum, but it is the whole remainder
    EXPECT_EQ(eng.execute_sale(world.order(pool, 50000)).status, SaleOutcome::Status::Accepted);
}

TEST_F(E2Test, PoolNotOpenForDraftAndClosed) {
    const i64 draft = world.make_pool(2500, 100000, /*open=*/false);
    eng.record_contribution(draft, world.farmer[0], 500000, world.manager);   // deposits are allowed on a draft
    SaleOutcome o = eng.execute_sale(world.order(draft, 100000));
    ASSERT_EQ(o.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(o.reason, Reason::PoolNotOpen);

    const i64 pool = world.make_worked_example_pool();
    eng.change_status(pool, "closed", world.manager);
    o = eng.execute_sale(world.order(pool, 100000));
    ASSERT_EQ(o.status, SaleOutcome::Status::Rejected);
    EXPECT_EQ(o.reason, Reason::PoolNotOpen);
    EXPECT_EQ(snapshot(pool).at("sale"), 0);
}

TEST_F(E2Test, BusyWhenTheRowLockCannotBeTaken_NothingChanges) {
    e2::Engine fast(config(4, 200));
    const i64 pool = world.make_worked_example_pool();
    const auto before = snapshot(pool);
    pqxx::connection holder(super_conninfo());
    pqxx::work hold(holder);
    hold.exec_params("SELECT 1 FROM pool WHERE pool_id = $1 FOR UPDATE", pool);   // someone else holds the pool lock
    const SaleOutcome o = fast.execute_sale(world.order(pool, 500000));
    EXPECT_EQ(o.status, SaleOutcome::Status::Transient);   // E3 retries up to 3 times, then reports BUSY
    hold.abort();
    EXPECT_EQ(snapshot(pool), before);
    EXPECT_EQ(fast.execute_sale(world.order(pool, 500000)).status, SaleOutcome::Status::Accepted);
}

// ---- duplicate request_id ---------------------------------------------------------------------------------------

TEST_F(E2Test, DuplicateRequestIdReturnsTheOriginalResultAndNeverSellsTwice) {
    const i64 pool = world.make_worked_example_pool();
    const e2::SaleRequest req = world.order(pool, 500000);
    const SaleOutcome first = eng.execute_sale(req);
    ASSERT_EQ(first.status, SaleOutcome::Status::Accepted);
    const auto after_first = snapshot(pool);

    const SaleOutcome second = eng.execute_sale(req);
    ASSERT_EQ(second.status, SaleOutcome::Status::Accepted);
    EXPECT_TRUE(second.receipt.duplicate);
    EXPECT_EQ(second.receipt.sale_id, first.receipt.sale_id);
    EXPECT_EQ(second.receipt.receipt_no, first.receipt.receipt_no);
    EXPECT_EQ(second.receipt.gross_paise, first.receipt.gross_paise);
    EXPECT_EQ(second.receipt.stock_after_g, first.receipt.stock_after_g);   // the original stock, not today's
    ASSERT_EQ(second.receipt.payouts.size(), 3u);
    EXPECT_EQ(second.receipt.payouts[1].paise, 500000);
    EXPECT_EQ(snapshot(pool), after_first);

    // the same id still answers after the pool changed
    eng.change_price(pool, 2000, world.manager);
    EXPECT_TRUE(eng.execute_sale(req).receipt.duplicate);
}

TEST_F(E2Test, RequestIdCannotBeUsedByAnotherBuyerOrForAnotherOrder) {
    const i64 pool = world.make_worked_example_pool();
    const e2::SaleRequest req = world.order(pool, 500000);
    ASSERT_EQ(eng.execute_sale(req).status, SaleOutcome::Status::Accepted);
    e2::SaleRequest thief = req;
    thief.buyer_id = thief.actor_user_id = world.buyer2;
    try { eng.execute_sale(thief); FAIL() << "expected DomainError"; }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "REQUEST_ID_CONFLICT"); }
    e2::SaleRequest other = req;
    other.quantity_g = 100000;
    EXPECT_THROW(eng.execute_sale(other), e2::DomainError);
}

TEST_F(E2Test, ConcurrentTwinsCreateExactlyOneSale) {
    e2::Engine many(config(12));
    const i64 pool = world.make_worked_example_pool();
    const e2::SaleRequest req = world.order(pool, 500000);
    std::vector<std::thread> threads;
    std::atomic<int> accepted{0}, duplicates{0};
    for (int i = 0; i < 10; ++i)
        threads.emplace_back([&] {
            const SaleOutcome o = many.execute_sale(req);
            if (o.status == SaleOutcome::Status::Accepted) { ++accepted; if (o.receipt.duplicate) ++duplicates; }
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(accepted.load(), 10);
    EXPECT_EQ(duplicates.load(), 9);
    EXPECT_EQ(snapshot(pool).at("sale"), 1);
    EXPECT_EQ(snapshot(pool).at("stock"), 1500000);
}

// ---- T-FR-09: a failure injected after each step of a sale -------------------------------------------------------

TEST_F(E2Test, FailureAfterAnyStepRollsBackEverything) {
    const i64 pool = world.make_worked_example_pool();
    const auto before = snapshot(pool);
    for (const SaleStep step : kAllSteps) {
        const e2::SaleRequest req = world.order(pool, 500000);
        const e2::SaleStepHook hook = [step](SaleStep s) { if (s == step) throw std::runtime_error("injected failure"); };
        EXPECT_THROW(eng.execute_sale(req, &hook), std::runtime_error) << "step " << static_cast<int>(step);
        EXPECT_EQ(snapshot(pool), before) << "state changed after failure at step " << static_cast<int>(step);
        EXPECT_TRUE(eng.reconciliation(pool).ok);
    }
    // the failed attempts left nothing behind, so the same request can now be made for real
    const e2::SaleRequest again = world.order(pool, 500000);
    const e2::SaleStepHook failing = [](SaleStep s) { if (s == SaleStep::Notified) throw std::runtime_error("x"); };
    EXPECT_THROW(eng.execute_sale(again, &failing), std::runtime_error);
    EXPECT_EQ(eng.execute_sale(again).status, SaleOutcome::Status::Accepted);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

TEST_F(E2Test, RolledBackSaleNeverNotifies_CommittedSaleDoes) {
    const i64 pool = world.make_worked_example_pool();
    pqxx::connection listen_conn(super_conninfo());
    Listener listener(listen_conn);
    listener.drain(50);
    listener.payloads.clear();

    const e2::SaleStepHook failing = [](SaleStep s) { if (s == SaleStep::Notified) throw std::runtime_error("late failure"); };
    EXPECT_THROW(eng.execute_sale(world.order(pool, 500000), &failing), std::runtime_error);
    listener.drain(300);
    EXPECT_TRUE(listener.payloads.empty()) << "a rolled-back sale must not notify";

    ASSERT_EQ(eng.execute_sale(world.order(pool, 500000)).status, SaleOutcome::Status::Accepted);
    listener.drain(2000);
    ASSERT_EQ(listener.payloads.size(), 1u);
    EXPECT_EQ(listener.payloads[0].substr(listener.payloads[0].find(':') + 1), std::to_string(pool));
}

TEST_F(E2Test, CorruptedBalanceFailsReconciliationAndRollsBack) {
    const i64 pool = world.make_worked_example_pool();
    tamper("UPDATE pool_seller_balance SET remaining_g = remaining_g + 1000 WHERE pool_id = $1 AND seller_id = $2", pool, world.farmer[0]);
    const auto before = snapshot(pool);
    try {
        eng.execute_sale(world.order(pool, 500000));
        FAIL() << "expected ReconciliationFailed";
    } catch (const e2::ReconciliationFailed& e) {
        ASSERT_FALSE(e.failures().empty());
        EXPECT_EQ(e.failures()[0].invariant, 1);   // stock no longer equals the sum of farmer balances
    }
    EXPECT_EQ(snapshot(pool), before);
    EXPECT_FALSE(eng.reconciliation(pool).ok);
    const e2::IntegrityResult ic = eng.integrity_check(pool);
    EXPECT_FALSE(ic.ok);
    EXPECT_EQ(ic.first_broken->reason, "INVARIANT");
}

TEST_F(E2Test, BalancesBelowStockAreCaughtNotSold) {
    const i64 pool = world.make_worked_example_pool();
    tamper("UPDATE pool_seller_balance SET remaining_g = 0 WHERE pool_id = $1 AND seller_id = $2", pool, world.farmer[1]);
    const auto before = snapshot(pool);
    EXPECT_THROW(eng.execute_sale(world.order(pool, 1900000)), e2::ReconciliationFailed);   // more than the balances cover
    EXPECT_EQ(snapshot(pool), before);
}

// ---- concurrency at the E2 level (E3 adds the queue; the row lock alone must already be correct) ------------------

TEST_F(E2Test, RowLockPreventsOversellWithoutAnyQueue) {
    e2::Engine many(config(10));
    const i64 pool = world.make_pool(2500, 100000);
    eng.record_contribution(pool, world.farmer[0], 1000000, world.manager);   // 10 quintal
    std::atomic<int> accepted{0}, rejected{0}, other{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 24; ++i)
        threads.emplace_back([&] {
            const SaleOutcome o = many.execute_sale(world.order(pool, 100000));   // 1 quintal each
            if (o.status == SaleOutcome::Status::Accepted) ++accepted;
            else if (o.status == SaleOutcome::Status::Rejected) ++rejected;
            else ++other;
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(accepted.load(), 10);
    EXPECT_EQ(rejected.load() + other.load(), 14);
    EXPECT_EQ(snapshot(pool).at("stock"), 0);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

TEST_F(E2Test, FarmerWithNothingLeftGetsNoRowsInALaterSale) {
    const i64 pool = world.make_pool(2500, 1);
    eng.record_contribution(pool, world.farmer[0], 1000, world.manager);
    ASSERT_EQ(eng.execute_sale(world.order(pool, 1000)).status, SaleOutcome::Status::Accepted);   // farmer A now owns 0
    eng.record_contribution(pool, world.farmer[1], 2000, world.manager);
    const SaleOutcome o = eng.execute_sale(world.order(pool, 500));
    ASSERT_EQ(o.status, SaleOutcome::Status::Accepted);
    ASSERT_EQ(o.receipt.payouts.size(), 1u);
    EXPECT_EQ(o.receipt.payouts[0].seller_id, world.farmer[1]);
    EXPECT_EQ(o.receipt.payouts[0].alloc_g, 500);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

// ---- tampering (T-NFR-03) ---------------------------------------------------------------------------------------

TEST_F(E2Test, TamperedLedgerRowIsNamedAtTheExactEntry) {
    const i64 pool = world.make_worked_example_pool();
    ASSERT_EQ(eng.execute_sale(world.order(pool, 500000)).status, SaleOutcome::Status::Accepted);
    ASSERT_TRUE(eng.integrity_check(pool).ok);

    // change one payout in the money ledger (entry 6, the sale) as a superuser with triggers off
    tamper("UPDATE financial_ledger SET amount_paise = amount_paise + 1 WHERE pool_id = $1 AND entry_type = 'SELLER_PAYOUT' AND seller_id = $2",
           pool, world.farmer[0]);
    e2::IntegrityResult r = eng.integrity_check(pool);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 6);
    EXPECT_EQ(r.first_broken->event_type, "SALE");
    EXPECT_EQ(r.first_broken->reason, "HASH_MISMATCH");
}

TEST_F(E2Test, TamperEarlierThanTheSaleIsReportedFirst) {
    const i64 pool = world.make_worked_example_pool();
    ASSERT_EQ(eng.execute_sale(world.order(pool, 500000)).status, SaleOutcome::Status::Accepted);
    tamper("UPDATE inventory_ledger SET delta_g = delta_g + 1 WHERE pool_id = $1 AND movement_type = 'IN' AND resulting_stock_g = $2", pool, 1400000);   // the 800 kg deposit: chain is created(1) opened(2) deposits(3,4,5) sale(6)
    const e2::IntegrityResult r = eng.integrity_check(pool);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->sequence_no, 4);
    EXPECT_EQ(r.first_broken->event_type, "CONTRIBUTION");
    EXPECT_EQ(r.first_broken->reason, "HASH_MISMATCH");
}

TEST_F(E2Test, TamperedPriceChangeRecordIsDetected) {
    const i64 pool = world.make_worked_example_pool();
    eng.change_price(pool, 2400, world.manager);
    ASSERT_TRUE(eng.integrity_check(pool).ok);
    tamper("UPDATE pool_change SET new_value = '1' WHERE pool_id = $1 AND field = 'PRICE'", pool);
    const e2::IntegrityResult r = eng.integrity_check(pool);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->event_type, "PRICE_CHANGED");
    EXPECT_EQ(r.first_broken->reason, "HASH_MISMATCH");
}

TEST_F(E2Test, DeletedAuditEntriesAreReportedAsGaps) {
    const i64 pool = world.make_worked_example_pool();
    ASSERT_EQ(eng.execute_sale(world.order(pool, 500000)).status, SaleOutcome::Status::Accepted);
    // remove the newest entry: no gap inside the chain, but an event now has no seal
    tamper("DELETE FROM ledger_entry WHERE pool_id = $1 AND sequence_no = $2", pool, 6);
    e2::IntegrityResult r = eng.integrity_check(pool);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->reason, "GAP");
    EXPECT_EQ(r.first_broken->sequence_no, 6);
    EXPECT_EQ(r.first_broken->event_type, "SALE");
}

TEST_F(E2Test, DeletedMiddleEntryIsAGap) {
    const i64 pool = world.make_worked_example_pool();
    tamper("DELETE FROM ledger_entry WHERE pool_id = $1 AND sequence_no = $2", pool, 3);   // the first deposit's seal
    const e2::IntegrityResult r = eng.integrity_check(pool);
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(r.first_broken->reason, "GAP");
    EXPECT_EQ(r.first_broken->sequence_no, 4);   // entry 4 now sits where 3 should be
}

// ---- other operations -------------------------------------------------------------------------------------------

TEST_F(E2Test, UsersAndValidation) {
    const std::string name = unique_name("dup");
    EXPECT_GT(eng.create_user({"X", name, "FARMER", "hash"}), 0);
    try { eng.create_user({"Y", name, "BUYER", "hash"}); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "DUPLICATE_USERNAME"); }
    EXPECT_THROW(eng.create_user({"X", "Bad Name!", "FARMER", "hash"}), e2::DomainError);
    EXPECT_THROW(eng.create_user({"X", unique_name("u"), "ADMIN", "hash"}), e2::DomainError);

    const i64 pool = world.make_pool();
    EXPECT_THROW(eng.record_contribution(pool, world.farmer[0], 0, world.manager), e2::DomainError);
    EXPECT_THROW(eng.record_contribution(pool, world.farmer[0], -5, world.manager), e2::DomainError);
    EXPECT_THROW(eng.record_contribution(pool, world.buyer, 1000, world.manager), e2::DomainError);   // a buyer is not a farmer
    EXPECT_THROW(eng.record_contribution(999999999, world.farmer[0], 1000, world.manager), e2::DomainError);
    EXPECT_THROW(eng.execute_sale(world.order(pool, 0)), e2::DomainError);
    EXPECT_THROW(eng.execute_sale(world.order(pool, 1000, 2500, "not-a-uuid")), e2::DomainError);
    EXPECT_THROW(eng.execute_sale(world.order(999999999, 1000)), e2::DomainError);
    e2::NewPool bad;
    bad.commodity_id = world.commodity; bad.variety = "v"; bad.range_no = 4; bad.storage_location = "s"; bad.min_order_g = 1; bad.price_paise_per_kg = 1;
    EXPECT_THROW(eng.create_pool(bad, world.manager), e2::DomainError);
    bad.range_no = 1; bad.price_paise_per_kg = 0;
    EXPECT_THROW(eng.create_pool(bad, world.manager), e2::DomainError);
}

TEST_F(E2Test, StatusRules_PhotoRequiredClosedRefusesDepositsReopenNeedsPhoto) {
    e2::NewPool np;
    np.commodity_id = world.commodity; np.variety = "v"; np.range_no = 2; np.storage_location = "s"; np.min_order_g = 1000; np.price_paise_per_kg = 2000;
    const i64 pool = eng.create_pool(np, world.manager);
    try { eng.change_status(pool, "open", world.manager); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "PHOTO_REQUIRED"); }
    eng.add_photo(pool, world.manager, "photos/x.jpg", std::string(64, 'b'));
    eng.change_status(pool, "open", world.manager);
    eng.record_contribution(pool, world.farmer[0], 5000, world.manager);

    eng.change_status(pool, "closed", world.manager);
    try { eng.record_contribution(pool, world.farmer[0], 5000, world.manager); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "POOL_NOT_OPEN"); }
    try { eng.change_status(pool, "draft", world.manager); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "INVALID_TRANSITION"); }
    try { eng.change_status(pool, "closed", world.manager); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "NO_CHANGE"); }
    eng.change_status(pool, "open", world.manager);   // the Manager may reopen
    EXPECT_NO_THROW(eng.record_contribution(pool, world.farmer[0], 5000, world.manager));
    EXPECT_TRUE(eng.integrity_check(pool).ok);
    EXPECT_EQ(snapshot(pool).at("pool_change"), 3);   // opened, closed, reopened
}

TEST_F(E2Test, PriceChangeIsAnAuditedEventAndAffectsOnlyLaterSales) {
    const i64 pool = world.make_worked_example_pool();
    const auto before = snapshot(pool);
    eng.change_price(pool, 2400, world.manager);
    const auto after = snapshot(pool);
    EXPECT_EQ(after.at("event"), before.at("event") + 1);
    EXPECT_EQ(after.at("ledger_entry"), before.at("ledger_entry") + 1);
    EXPECT_EQ(after.at("pool_change"), before.at("pool_change") + 1);
    EXPECT_EQ(after.at("inventory_ledger"), before.at("inventory_ledger"));   // not a ledger row
    try { eng.change_price(pool, 2400, world.manager); FAIL(); }
    catch (const e2::DomainError& e) { EXPECT_EQ(e.code(), "NO_CHANGE"); }
    EXPECT_THROW(eng.change_price(pool, 0, world.manager), e2::DomainError);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

TEST_F(E2Test, ManagerCanPlaceASaleForABuyer) {
    const i64 pool = world.make_worked_example_pool();
    e2::SaleRequest req = world.order(pool, 500000);
    req.buyer_id = world.buyer2;
    req.actor_user_id = world.manager;   // who placed it is recorded in event.actor_user_id
    ASSERT_EQ(eng.execute_sale(req).status, SaleOutcome::Status::Accepted);
    pqxx::connection admin(super_conninfo());
    pqxx::nontransaction n(admin);
    const pqxx::row r = n.exec_params1("SELECT e.actor_user_id, s.buyer_id FROM sale s JOIN event e USING (event_id) WHERE s.request_id = $1::uuid", req.request_id);
    EXPECT_EQ(r[0].as<i64>(), world.manager);
    EXPECT_EQ(r[1].as<i64>(), world.buyer2);
}
