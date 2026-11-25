#include <algorithm>
#include <cstdio>

#include "internal.hpp"

namespace e2 {
using namespace detail;

namespace {

constexpr i64 kNoSales = -1;               // reconcile(): an event id no sale has, so INV-4 checks nothing
constexpr i64 kMaxGrams = 1000000000000000LL;   // 10^15 g sanity limit per deposit and per pool
constexpr i64 kMaxPrice = 1000000000LL;         // paise per kg sanity limit

void require(bool ok, const char* code, const std::string& message) {
    if (!ok) throw DomainError(code, message);
}

bool is_hex_lower(const std::string& s, std::size_t n) {
    return s.size() == n && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_uuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool username_ok(const std::string& u) {
    if (u.size() < 3 || u.size() > 50) return false;
    return std::all_of(u.begin(), u.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; });
}

bool phone_ok(const std::string& p) {   // a 10-digit Indian mobile number: starts with 6, 7, 8 or 9
    return p.size() == 10 && p[0] >= '6' && p[0] <= '9' && std::all_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool id_doc_ok(const std::optional<std::string>& type, const std::optional<std::string>& last4) {
    if (type.has_value() != last4.has_value()) return false;
    if (!type) return true;
    static const char* kTypes[] = {"APMC_REG", "LAND_RECORD", "AADHAAR", "TRADE_LICENCE", "GST", "PAN"};
    if (std::none_of(std::begin(kTypes), std::end(kTypes), [&](const char* k) { return *type == k; })) return false;
    return last4->size() == 4 && std::all_of(last4->begin(), last4->end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}

std::string receipt_no(i64 sale_id) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "R-%04lld", static_cast<long long>(sale_id));
    return buf;
}

// The stored result of an earlier request with the same request_id, if any (spec §5.1 step 1).
std::optional<Receipt> find_existing_sale(pqxx::transaction_base& tx, const SaleRequest& req) {
    const pqxx::result r = tx.exec_params(
        "SELECT s.sale_id, s.pool_id, s.buyer_id, s.quantity_g, s.unit_price_paise_per_kg, s.gross_paise, " +
        utc_micros("e.created_at") + " AS ts, p.storage_location, "
        "(SELECT i.resulting_stock_g FROM inventory_ledger i WHERE i.event_id = s.event_id) AS stock_after "
        "FROM sale s JOIN event e ON e.event_id = s.event_id JOIN pool p ON p.pool_id = s.pool_id "
        "WHERE s.request_id = $1::uuid", req.request_id);
    if (r.empty()) return std::nullopt;
    const pqxx::row& row = r[0];
    // The same id must never reveal another buyer's sale or be reused for a different order.
    if (row["pool_id"].as<i64>() != req.pool_id || row["buyer_id"].as<i64>() != req.buyer_id ||
        row["quantity_g"].as<i64>() != req.quantity_g)
        throw DomainError("REQUEST_ID_CONFLICT", "request_id was already used for a different order");
    Receipt rc;
    rc.sale_id = row["sale_id"].as<i64>();
    rc.receipt_no = receipt_no(rc.sale_id);
    rc.pool_id = req.pool_id;
    rc.quantity_g = row["quantity_g"].as<i64>();
    rc.unit_price_paise_per_kg = row["unit_price_paise_per_kg"].as<i64>();
    rc.gross_paise = row["gross_paise"].as<i64>();
    rc.created_at = row["ts"].as<std::string>().substr(0, 19) + "Z";
    rc.storage_location = row["storage_location"].as<std::string>();
    rc.stock_after_g = row["stock_after"].as<i64>();
    rc.duplicate = true;
    for (const auto& p : tx.exec_params(
             "SELECT seller_id, quantity_allocated_g, payout_amount_paise FROM payout WHERE sale_id = $1 ORDER BY seller_id",
             rc.sale_id))
        rc.payouts.push_back({p[0].as<i64>(), p[1].as<i64>(), p[2].as<i64>()});
    return rc;
}

SaleOutcome accepted(Receipt r) {
    SaleOutcome o;
    o.status = SaleOutcome::Status::Accepted;
    o.receipt = std::move(r);
    return o;
}

SaleOutcome rejected(Reason why) {
    SaleOutcome o;
    o.status = SaleOutcome::Status::Rejected;
    o.reason = why;
    return o;
}

}  // namespace

Engine::Engine(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Engine::~Engine() = default;

// ---- accounts --------------------------------------------------------------------------------------------------

i64 Engine::create_user(const NewUser& u) {
    require(username_ok(u.username), "INVALID", "username must be 3-50 characters: a-z 0-9 . _ -");
    require(!u.name.empty() && u.name.size() <= 100, "INVALID", "name must be 1-100 characters");
    require(u.type == "MANAGER" || u.type == "FARMER" || u.type == "BUYER", "INVALID", "unknown user type");
    require(!u.password_hash.empty(), "INVALID", "password hash is missing");
    require(!u.phone || phone_ok(*u.phone), "INVALID", "mobile number must be 10 digits starting with 6, 7, 8 or 9");
    require(id_doc_ok(u.id_doc_type, u.id_last4), "INVALID", "identity document type and its last four characters go together");
    require(u.type != "MANAGER" || !u.pending, "INVALID", "a Pool Manager cannot be pending");
    try {
        return transact(*impl_, [&](pqxx::work& tx) {
            // the mobile number is the sign-in name for most people, so say which of the two was taken
            if (u.phone && !tx.exec_params("SELECT 1 FROM app_user WHERE phone = $1", *u.phone).empty())
                throw DomainError("DUPLICATE_PHONE", "that mobile number is already registered");
            if (!tx.exec_params("SELECT 1 FROM app_user WHERE username = $1 OR phone = $1", u.username).empty())
                throw DomainError("DUPLICATE_USERNAME", "that username is already taken");
            const i64 id = tx.exec_params1(
                "INSERT INTO app_user(username, password_hash, user_type, phone, approval_status, must_change_password, id_doc_type, id_last4) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING user_id",
                u.username, u.password_hash, u.type, u.phone, u.pending ? "PENDING" : "APPROVED", u.must_change_password,
                u.id_doc_type, u.id_last4)[0].as<i64>();
            if (u.type == "MANAGER") tx.exec_params0("INSERT INTO pool_manager(user_id, manager_name) VALUES ($1, $2)", id, u.name);
            else if (u.type == "FARMER") tx.exec_params0("INSERT INTO seller(seller_id, seller_name) VALUES ($1, $2)", id, u.name);
            else tx.exec_params0("INSERT INTO buyer(buyer_id, buyer_name) VALUES ($1, $2)", id, u.name);
            return id;
        });
    } catch (const pqxx::unique_violation& e) {
        if (std::string(e.what()).find("phone") != std::string::npos) throw DomainError("DUPLICATE_PHONE", "that mobile number is already registered");
        throw DomainError("DUPLICATE_USERNAME", "that username is already taken");
    }
}

// ---- market prices (Agmarknet via data.gov.in) -------------------------------------------------------------------------

std::size_t Engine::import_market_prices(const std::vector<MarketPriceRow>& rows) {
    return transact(*impl_, [&](pqxx::work& tx) {
        std::size_t written = 0;
        for (const MarketPriceRow& r : rows) {
            const bool ok = r.commodity_id > 0 && !r.market.empty() && !r.variety.empty() && r.variety.size() <= 60 && r.market.size() <= 100 &&
                            r.min_paise_per_kg > 0 && r.max_paise_per_kg >= r.min_paise_per_kg && r.modal_paise_per_kg > 0 &&
                            r.arrival_date.size() == 10 && r.arrival_date[4] == '-' && r.arrival_date[7] == '-';
            if (!ok) continue;
            tx.exec_params0(
                "INSERT INTO market_price(commodity_id, state, district, market, variety, market_grade, arrival_date, min_paise_per_kg, max_paise_per_kg, modal_paise_per_kg) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7::date, $8, $9, $10) "
                "ON CONFLICT (commodity_id, state, district, market, variety, market_grade, arrival_date) DO UPDATE SET "
                "min_paise_per_kg = EXCLUDED.min_paise_per_kg, max_paise_per_kg = EXCLUDED.max_paise_per_kg, "
                "modal_paise_per_kg = EXCLUDED.modal_paise_per_kg, fetched_at = now()",
                r.commodity_id, r.state, r.district, r.market, r.variety, r.market_grade, r.arrival_date, r.min_paise_per_kg, r.max_paise_per_kg, r.modal_paise_per_kg);
            tx.exec_params0("INSERT INTO variety_catalog(commodity_id, variety) VALUES ($1, $2) ON CONFLICT DO NOTHING", r.commodity_id, r.variety);
            ++written;
        }
        return written;
    });
}

// ---- the Pool Manager's people tools ---------------------------------------------------------------------------------

namespace {
void require_person(pqxx::work& tx, i64 user_id) {   // Farmer or Buyer only: Managers are never changed through these
    require(!tx.exec_params("SELECT 1 FROM app_user WHERE user_id = $1 AND user_type IN ('FARMER','BUYER') FOR UPDATE", user_id).empty(),
            "NOT_FOUND", "person not found");
}
}  // namespace

void Engine::approve_user(i64 user_id, i64 manager_id) {
    transact(*impl_, [&](pqxx::work& tx) {
        require_person(tx, user_id);
        require(!tx.exec_params("SELECT 1 FROM app_user WHERE user_id = $1 AND id_doc_type IS NOT NULL", user_id).empty(), "NO_ID_DOCUMENT",
                "an identity document must be recorded before approval");
        tx.exec_params0("UPDATE app_user SET approval_status = 'APPROVED', verified_by = $2, verified_at = now() WHERE user_id = $1", user_id, manager_id);
    });
}

void Engine::set_user_active(i64 user_id, bool active) {
    transact(*impl_, [&](pqxx::work& tx) {
        require_person(tx, user_id);
        tx.exec_params0("UPDATE app_user SET is_active = $2 WHERE user_id = $1", user_id, active);
        if (!active) tx.exec_params0("DELETE FROM session_token WHERE user_id = $1", user_id);
    });
}

void Engine::update_user(i64 user_id, const UserEdit& e) {
    require(!e.name || (!e.name->empty() && e.name->size() <= 100), "INVALID", "name must be 1-100 characters");
    require(!e.phone || phone_ok(*e.phone), "INVALID", "mobile number must be 10 digits starting with 6, 7, 8 or 9");
    require(id_doc_ok(e.id_doc_type, e.id_last4), "INVALID", "identity document type and its last four characters go together");
    try {
        transact(*impl_, [&](pqxx::work& tx) {
            require_person(tx, user_id);
            if (e.name) {
                tx.exec_params0("UPDATE seller SET seller_name = $2 WHERE seller_id = $1", user_id, *e.name);
                tx.exec_params0("UPDATE buyer SET buyer_name = $2 WHERE buyer_id = $1", user_id, *e.name);
            }
            if (e.phone) {
                // a number someone already uses as their sign-in name would make two people answer to one sign-in
                require(tx.exec_params("SELECT 1 FROM app_user WHERE username = $1 AND user_id <> $2", *e.phone, user_id).empty(), "DUPLICATE_PHONE", "that mobile number is already registered");
                tx.exec_params0("UPDATE app_user SET phone = $2 WHERE user_id = $1", user_id, *e.phone);
            }
            if (e.id_doc_type)   // a changed document is a new document: it has to be checked again
                tx.exec_params0("UPDATE app_user SET id_doc_type = $2, id_last4 = $3, verified_by = NULL, verified_at = NULL, approval_status = 'PENDING' WHERE user_id = $1",
                                user_id, *e.id_doc_type, *e.id_last4);
        });
    } catch (const pqxx::unique_violation&) {
        throw DomainError("DUPLICATE_PHONE", "that mobile number is already registered");
    }
}

// ---- pools -----------------------------------------------------------------------------------------------------

i64 Engine::create_pool(const NewPool& p, i64 actor) {
    require(p.range_no >= 1 && p.range_no <= 3, "INVALID", "Range must be 1, 2 or 3");
    require(!p.variety.empty() && p.variety.size() <= 60, "INVALID", "variety must be 1-60 characters");
    require(!p.storage_location.empty() && p.storage_location.size() <= 120, "INVALID", "storage location must be 1-120 characters");
    require(!p.packaging || p.packaging->size() <= 60, "INVALID", "packaging must be at most 60 characters");
    require(p.min_order_g > 0 && p.min_order_g <= kMaxGrams, "INVALID", "minimum order is out of range");
    require(p.price_paise_per_kg > 0 && p.price_paise_per_kg <= kMaxPrice, "INVALID", "price is out of range");
    require(!p.market || (!p.market->empty() && p.market->size() <= 80), "INVALID", "market must be 1-80 characters");
    return transact(*impl_, [&](pqxx::work& tx) {
        require(!tx.exec_params("SELECT 1 FROM commodity WHERE commodity_id = $1", p.commodity_id).empty(), "NOT_FOUND", "commodity not found");
        const i64 pool_id = tx.exec_params1(
            "INSERT INTO pool(commodity_id, variety, range_no, packaging, storage_location, market, min_order_g, display_unit, price_paise_per_kg) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, 'quintal', $8) RETURNING pool_id",
            p.commodity_id, p.variety, static_cast<int>(p.range_no), p.packaging, p.storage_location, p.market, p.min_order_g,
            p.price_paise_per_kg)[0].as<i64>();
        const EventRef ev = insert_event(tx, pool_id, "POOL_CREATED", actor);
        tx.exec_params0("INSERT INTO inventory_ledger(event_id, pool_id, movement_type, delta_g, resulting_stock_g) VALUES ($1, $2, 'OPEN', 0, 0)",
                        ev.event_id, pool_id);
        e1::EventRows rows;
        rows.inventory.push_back({"OPEN", 0, 0});
        seal_event(tx, pool_id, ev, "POOL_CREATED", rows);
        reconcile(tx, pool_id, kNoSales);
        notify_event(tx, ev.event_id, pool_id);
        return pool_id;
    });
}

ContributionResult Engine::record_contribution(i64 pool_id, i64 seller_id, i64 quantity_g, i64 actor) {
    require(quantity_g > 0 && quantity_g <= kMaxGrams, "INVALID", "quantity must be more than zero");
    return transact(*impl_, [&](pqxx::work& tx) {
        const PoolRow pool = lock_pool(tx, pool_id);
        require(pool.status == "draft" || pool.status == "open", "POOL_NOT_OPEN", "this pool is closed");
        require(pool.total_stock_g <= kMaxGrams - quantity_g, "INVALID", "pool stock would be out of range");
        require(!tx.exec_params("SELECT 1 FROM seller WHERE seller_id = $1", seller_id).empty(), "NOT_FOUND", "farmer not found");
        require(!tx.exec_params("SELECT 1 FROM app_user WHERE user_id = $1 AND is_active AND approval_status = 'APPROVED'", seller_id).empty(),
                "FARMER_NOT_APPROVED", "this farmer's account is not approved");

        const EventRef ev = insert_event(tx, pool_id, "CONTRIBUTION", actor);
        tx.exec_params0("INSERT INTO contribution(event_id, pool_id, seller_id, quantity_g) VALUES ($1, $2, $3, $4)",
                        ev.event_id, pool_id, seller_id, quantity_g);
        const i64 balance = tx.exec_params1(
            "INSERT INTO pool_seller_balance(pool_id, seller_id, remaining_g) VALUES ($1, $2, $3) "
            "ON CONFLICT (pool_id, seller_id) DO UPDATE SET remaining_g = pool_seller_balance.remaining_g + EXCLUDED.remaining_g "
            "RETURNING remaining_g", pool_id, seller_id, quantity_g)[0].as<i64>();
        const i64 stock = tx.exec_params1("UPDATE pool SET total_stock_g = total_stock_g + $2 WHERE pool_id = $1 RETURNING total_stock_g",
                                          pool_id, quantity_g)[0].as<i64>();
        tx.exec_params0("INSERT INTO inventory_ledger(event_id, pool_id, movement_type, delta_g, resulting_stock_g) VALUES ($1, $2, 'IN', $3, $4)",
                        ev.event_id, pool_id, quantity_g, stock);
        tx.exec_params0("INSERT INTO ownership_ledger(event_id, pool_id, seller_id, delta_g, resulting_balance_g) VALUES ($1, $2, $3, $4, $5)",
                        ev.event_id, pool_id, seller_id, quantity_g, balance);
        e1::EventRows rows;
        rows.inventory.push_back({"IN", quantity_g, stock});
        rows.ownership.push_back({seller_id, quantity_g, balance});
        seal_event(tx, pool_id, ev, "CONTRIBUTION", rows);
        reconcile(tx, pool_id, kNoSales);
        notify_event(tx, ev.event_id, pool_id);
        return ContributionResult{ev.event_id, stock, balance};
    });
}

i64 Engine::add_photo(i64 pool_id, i64 uploaded_by, const std::string& file_path, const std::string& sha256_hex) {
    require(!file_path.empty() && file_path.size() <= 500 && file_path.find("..") == std::string::npos, "INVALID", "bad photo path");
    require(is_hex_lower(sha256_hex, 64), "INVALID", "photo checksum must be 64 lowercase hex characters");
    return transact(*impl_, [&](pqxx::work& tx) {
        lock_pool(tx, pool_id);   // serialises with a status change that checks for photos
        return tx.exec_params1("INSERT INTO pool_photo(pool_id, file_path, file_sha256, uploaded_by) VALUES ($1, $2, $3, $4) RETURNING photo_id",
                               pool_id, file_path, sha256_hex, uploaded_by)[0].as<i64>();
    });
}

void Engine::change_price(i64 pool_id, i64 new_price, i64 actor) {
    require(new_price > 0 && new_price <= kMaxPrice, "INVALID", "price is out of range");
    transact(*impl_, [&](pqxx::work& tx) {
        const PoolRow pool = lock_pool(tx, pool_id);
        require(pool.price_paise_per_kg != new_price, "NO_CHANGE", "the price is already that value");
        tx.exec_params0("UPDATE pool SET price_paise_per_kg = $2 WHERE pool_id = $1", pool_id, new_price);
        const EventRef ev = insert_event(tx, pool_id, "PRICE_CHANGED", actor);
        const std::string old_v = std::to_string(pool.price_paise_per_kg), new_v = std::to_string(new_price);
        tx.exec_params0("INSERT INTO pool_change(event_id, pool_id, field, old_value, new_value, changed_by) VALUES ($1, $2, 'PRICE', $3, $4, $5)",
                        ev.event_id, pool_id, old_v, new_v, actor);
        e1::EventRows rows;
        rows.change = e1::PoolChangeRow{"PRICE", old_v, new_v, actor};
        seal_event(tx, pool_id, ev, "PRICE_CHANGED", rows);
        reconcile(tx, pool_id, kNoSales);
        notify_event(tx, ev.event_id, pool_id);
    });
}

void Engine::change_status(i64 pool_id, const std::string& new_status, i64 actor) {
    require(new_status == "draft" || new_status == "open" || new_status == "closed", "INVALID", "unknown status");
    try {
        transact(*impl_, [&](pqxx::work& tx) {
            const PoolRow pool = lock_pool(tx, pool_id);
            require(pool.status != new_status, "NO_CHANGE", "the pool already has that status");
            const bool allowed = (pool.status == "draft" && new_status == "open") ||
                                 (pool.status == "open" && new_status == "closed") ||
                                 (pool.status == "closed" && new_status == "open");
            require(allowed, "INVALID_TRANSITION", "that status change is not allowed");
            if (new_status == "open")
                require(!tx.exec_params("SELECT 1 FROM pool_photo WHERE pool_id = $1 LIMIT 1", pool_id).empty(),
                        "PHOTO_REQUIRED", "a pool needs at least one photo before it can be opened");
            tx.exec_params0("UPDATE pool SET status = $2 WHERE pool_id = $1", pool_id, new_status);
            const EventRef ev = insert_event(tx, pool_id, "STATUS_CHANGED", actor);
            tx.exec_params0("INSERT INTO pool_change(event_id, pool_id, field, old_value, new_value, changed_by) VALUES ($1, $2, 'STATUS', $3, $4, $5)",
                            ev.event_id, pool_id, pool.status, new_status, actor);
            e1::EventRows rows;
            rows.change = e1::PoolChangeRow{"STATUS", pool.status, new_status, actor};
            seal_event(tx, pool_id, ev, "STATUS_CHANGED", rows);
            reconcile(tx, pool_id, kNoSales);
            notify_event(tx, ev.event_id, pool_id);
        });
    } catch (const pqxx::check_violation& e) {
        // The database rule is the backstop for the explicit photo check above; any other check violation is a real error.
        if (std::string(e.what()).find("without a photo") != std::string::npos)
            throw DomainError("PHOTO_REQUIRED", "a pool needs at least one photo before it can be opened");
        throw;
    }
}

// ---- the sale (spec §5.1) --------------------------------------------------------------------------------------

SaleOutcome Engine::execute_sale(const SaleRequest& req, const SaleStepHook* hook) {
    require(is_uuid(req.request_id), "INVALID", "request_id must be a UUID");
    require(req.quantity_g > 0 && req.quantity_g <= kMaxGrams, "INVALID", "quantity must be more than zero");
    require(req.confirmed_price_paise_per_kg > 0, "INVALID", "confirmed price is missing");
    const auto step = [hook](SaleStep s) { if (hook && *hook) (*hook)(s); };

    auto lease = impl_->pool.acquire();
    try {
        // 1. A sale with this request_id already exists: return its original result. Never create a second sale.
        {
            pqxx::nontransaction peek(lease.get());
            if (auto dup = find_existing_sale(peek, req)) return accepted(std::move(*dup));
        }

        // 2. BEGIN; lock the pool row.
        pqxx::work tx(lease.get());
        set_lock_timeout(tx, impl_->cfg.lock_timeout_ms);
        const PoolRow pool = lock_pool(tx, req.pool_id);
        // A twin request may have committed while this one waited for the lock.
        if (auto dup = find_existing_sale(tx, req)) return accepted(std::move(*dup));
        require(!tx.exec_params("SELECT 1 FROM buyer WHERE buyer_id = $1", req.buyer_id).empty(), "NOT_FOUND", "buyer not found");
        require(!tx.exec_params("SELECT 1 FROM app_user WHERE user_id = $1 AND is_active AND approval_status = 'APPROVED'", req.buyer_id).empty(),
                "BUYER_NOT_APPROVED", "this buyer's account is not approved");
        step(SaleStep::PoolLocked);

        // 3-6. Checks against the locked row, in the spec's order. A rejection leaves nothing behind (tx aborts).
        if (pool.status != "open") return rejected(Reason::PoolNotOpen);
        if (pool.total_stock_g == 0) return rejected(Reason::OutOfStock);
        if (req.quantity_g > pool.total_stock_g) {
            SaleOutcome o = rejected(Reason::InsufficientStock);
            o.stock_g = pool.total_stock_g;
            return o;
        }
        if (req.quantity_g < pool.min_order_g && req.quantity_g != pool.total_stock_g) {
            SaleOutcome o = rejected(Reason::BelowMinOrder);
            o.min_order_g = pool.min_order_g;
            return o;
        }
        if (req.confirmed_price_paise_per_kg != pool.price_paise_per_kg) {
            SaleOutcome o = rejected(Reason::PriceChanged);
            o.current_price_paise_per_kg = pool.price_paise_per_kg;
            return o;
        }
        step(SaleStep::Validated);

        // 7. Lock the balance rows in seller_id order; E1 computes the split.
        std::vector<e1::Weight> weights;
        for (const auto& row : tx.exec_params(
                 "SELECT seller_id, remaining_g FROM pool_seller_balance WHERE pool_id = $1 ORDER BY seller_id FOR UPDATE", req.pool_id))
            weights.push_back({row[0].as<i64>(), row[1].as<i64>()});
        e1::SaleSplit split;
        try {
            split = e1::compute_sale(req.quantity_g, pool.price_paise_per_kg, weights);
        } catch (const std::invalid_argument&) {
            throw ReconciliationFailed({{1, 0}});   // stock covers the order but the farmer balances do not: INV-1 is already broken
        } catch (const std::overflow_error&) {
            throw DomainError("INVALID", "the order total is too large");   // quantity * price does not fit in 64 bits
        }

        const EventRef ev = insert_event(tx, req.pool_id, "SALE", req.actor_user_id);
        step(SaleStep::EventInserted);

        const i64 sale_id = tx.exec_params1(
            "INSERT INTO sale(event_id, pool_id, buyer_id, request_id, quantity_g, unit_price_paise_per_kg, gross_paise) "
            "VALUES ($1, $2, $3, $4::uuid, $5, $6, $7) RETURNING sale_id",
            ev.event_id, req.pool_id, req.buyer_id, req.request_id, req.quantity_g, pool.price_paise_per_kg,
            split.gross_paise)[0].as<i64>();
        step(SaleStep::SaleInserted);

        // Farmers who own nothing get no rows (the ownership ledger forbids a zero change).
        std::vector<PayoutRow> payouts;
        for (std::size_t i = 0; i < split.alloc_g.size(); ++i)
            if (split.alloc_g[i].amount > 0)
                payouts.push_back({split.alloc_g[i].id, split.alloc_g[i].amount, split.payout_paise[i].amount});
        for (const PayoutRow& p : payouts)
            tx.exec_params0("INSERT INTO payout(sale_id, seller_id, quantity_allocated_g, payout_amount_paise) VALUES ($1, $2, $3, $4)",
                            sale_id, p.seller_id, p.alloc_g, p.paise);
        step(SaleStep::PayoutsInserted);

        e1::EventRows rows;
        for (const PayoutRow& p : payouts) {
            const i64 left = tx.exec_params1(
                "UPDATE pool_seller_balance SET remaining_g = remaining_g - $3 WHERE pool_id = $1 AND seller_id = $2 RETURNING remaining_g",
                req.pool_id, p.seller_id, p.alloc_g)[0].as<i64>();
            rows.ownership.push_back({p.seller_id, -p.alloc_g, left});
        }
        step(SaleStep::BalancesUpdated);

        const i64 stock_after = tx.exec_params1(
            "UPDATE pool SET total_stock_g = total_stock_g - $2 WHERE pool_id = $1 RETURNING total_stock_g",
            req.pool_id, req.quantity_g)[0].as<i64>();
        step(SaleStep::StockUpdated);

        tx.exec_params0("INSERT INTO inventory_ledger(event_id, pool_id, movement_type, delta_g, resulting_stock_g) VALUES ($1, $2, 'OUT', $3, $4)",
                        ev.event_id, req.pool_id, -req.quantity_g, stock_after);
        rows.inventory.push_back({"OUT", -req.quantity_g, stock_after});
        for (const auto& o : rows.ownership)
            tx.exec_params0("INSERT INTO ownership_ledger(event_id, pool_id, seller_id, delta_g, resulting_balance_g) VALUES ($1, $2, $3, $4, $5)",
                            ev.event_id, req.pool_id, o.seller_id, o.delta_g, o.resulting_balance_g);
        tx.exec_params0("INSERT INTO financial_ledger(event_id, pool_id, seller_id, entry_type, amount_paise) VALUES ($1, $2, NULL, 'SALE_GROSS', $3)",
                        ev.event_id, req.pool_id, split.gross_paise);
        rows.financial.push_back({"SALE_GROSS", std::nullopt, split.gross_paise});
        for (const PayoutRow& p : payouts) {
            tx.exec_params0("INSERT INTO financial_ledger(event_id, pool_id, seller_id, entry_type, amount_paise) VALUES ($1, $2, $3, 'SELLER_PAYOUT', $4)",
                            ev.event_id, req.pool_id, p.seller_id, p.paise);
            rows.financial.push_back({"SELLER_PAYOUT", p.seller_id, p.paise});
        }
        step(SaleStep::LedgersInserted);

        seal_event(tx, req.pool_id, ev, "SALE", rows);
        step(SaleStep::AuditSealed);

        reconcile(tx, req.pool_id, ev.event_id);   // INV-1..5, read back from the database
        step(SaleStep::Reconciled);

        notify_event(tx, ev.event_id, req.pool_id);
        step(SaleStep::Notified);

        tx.commit();

        Receipt rc;
        rc.sale_id = sale_id;
        rc.receipt_no = receipt_no(sale_id);
        rc.pool_id = req.pool_id;
        rc.quantity_g = req.quantity_g;
        rc.unit_price_paise_per_kg = pool.price_paise_per_kg;
        rc.gross_paise = split.gross_paise;
        rc.created_at = ev.created_at_utc.substr(0, 19) + "Z";
        rc.storage_location = pool.storage_location;
        rc.stock_after_g = stock_after;
        rc.payouts = std::move(payouts);
        return accepted(std::move(rc));
    } catch (const pqxx::sql_error& e) {
        if (is_transient(e)) {
            SaleOutcome o;
            o.status = SaleOutcome::Status::Transient;
            return o;
        }
        throw;
    }
}

}  // namespace e2
