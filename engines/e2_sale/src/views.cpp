// E2 reads for E4: accounts and sessions, reference data, and role-shaped pool, ledger, farmer and buyer views.
// All values are integers (grams, paise, permille); quality limits are decimal TEXT. All SQL uses bound parameters.
#include <cstdio>
#include <map>

#include "e1/wide_int.hpp"
#include "internal.hpp"

namespace e2 {
using namespace detail;
using json = nlohmann::json;

namespace {

std::string secs(const std::string& col) {
    return "to_char(" + col + " AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')";
}

json opt_str(const pqxx::field& f) { return f.is_null() ? json(nullptr) : json(f.as<std::string>()); }
json opt_i64(const pqxx::field& f) { return f.is_null() ? json(nullptr) : json(f.as<i64>()); }

// part / whole as permille (one decimal of a percent), rounded half-up, integers only.
i64 permille(i64 part, i64 whole) {
    if (whole <= 0 || part <= 0) return 0;
    const e1::u128 n = static_cast<e1::u128>(part) * 2000 + static_cast<e1::u128>(whole);
    return static_cast<i64>(n / (2 * static_cast<e1::u128>(whole)));
}

// One consistent snapshot for a view that runs several queries.
struct ReadTx {
    ConnectionPool::Lease lease;
    pqxx::work tx;
    explicit ReadTx(Engine::Impl& impl) : lease(impl.pool.acquire()), tx(lease.get()) {
        tx.exec0("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY");
    }
};

bool holds_share(pqxx::transaction_base& tx, i64 pool_id, i64 user_id) {
    return !tx.exec_params("SELECT 1 FROM pool_seller_balance WHERE pool_id = $1 AND seller_id = $2", pool_id, user_id).empty();
}

json photo_array(pqxx::transaction_base& tx, i64 pool_id, i64 limit) {
    json out = json::array();
    for (const auto& r : tx.exec_params("SELECT photo_id, file_path, " + secs("uploaded_at") +
                                        " FROM pool_photo WHERE pool_id = $1 ORDER BY photo_id DESC LIMIT $2", pool_id, limit))
        out.push_back({{"photo_id", r[0].as<i64>()}, {"url", "/" + r[1].as<std::string>()}, {"uploaded_at", r[2].as<std::string>()}});
    return out;
}

// Common pool fields for the list and the detail.
json pool_common(const pqxx::row& r) {
    json j;
    j["pool_id"] = r["pool_id"].as<i64>();
    j["commodity_id"] = r["commodity_id"].as<i64>();
    j["commodity"] = r["commodity"].as<std::string>();
    j["variety"] = r["variety"].as<std::string>();
    j["range_no"] = r["range_no"].as<int>();
    j["price_paise_per_kg"] = r["price_paise_per_kg"].as<i64>();
    j["stock_g"] = r["stock_g"].as<i64>();
    j["min_order_g"] = r["min_order_g"].as<i64>();
    j["packaging"] = opt_str(r["packaging"]);
    j["storage_location"] = r["storage_location"].as<std::string>();
    j["status"] = r["status"].as<std::string>();
    j["farmer_count"] = r["farmer_count"].as<i64>();
    j["first_deposit_at"] = opt_str(r["first_deposit"]);
    j["last_deposit_at"] = opt_str(r["last_deposit"]);
    j["last_sale_at"] = opt_str(r["last_sale_at"]);
    return j;
}

const char* kPoolSelect =
    "SELECT p.pool_id, p.commodity_id, c.name AS commodity, p.variety, p.range_no, p.price_paise_per_kg, "
    "p.total_stock_g AS stock_g, p.min_order_g, p.packaging, p.storage_location, p.status, "
    "(SELECT count(*) FROM pool_seller_balance b WHERE b.pool_id = p.pool_id) AS farmer_count, "
    "(SELECT to_char(min(x.contributed_at) AT TIME ZONE 'UTC','YYYY-MM-DD') FROM contribution x WHERE x.pool_id = p.pool_id) AS first_deposit, "
    "(SELECT to_char(max(x.contributed_at) AT TIME ZONE 'UTC','YYYY-MM-DD') FROM contribution x WHERE x.pool_id = p.pool_id) AS last_deposit, "
    "(SELECT to_char(max(s.created_at) AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') FROM sale s WHERE s.pool_id = p.pool_id) AS last_sale_at, "
    "(SELECT count(*) FROM ledger_entry l WHERE l.pool_id = p.pool_id) AS entries "
    "FROM pool p JOIN commodity c ON c.commodity_id = p.commodity_id ";

const char* kSoldRecently =
    "EXISTS (SELECT 1 FROM sale s2 WHERE s2.pool_id = p.pool_id AND s2.created_at > now() - interval '24 hours')";

}  // namespace

// ---- accounts and sessions -----------------------------------------------------------------------------------------

std::optional<AuthUser> Engine::find_user(const std::string& username) {
    ReadTx rt(*impl_);
    const pqxx::result r = rt.tx.exec_params(
        "SELECT u.user_id, u.username, u.password_hash, u.user_type, u.is_active, u.must_change_password, "
        "coalesce(m.manager_name, s.seller_name, b.buyer_name) AS name "
        "FROM app_user u LEFT JOIN pool_manager m ON m.user_id = u.user_id LEFT JOIN seller s ON s.seller_id = u.user_id "
        "LEFT JOIN buyer b ON b.buyer_id = u.user_id WHERE u.username = $1", username);
    if (r.empty()) return std::nullopt;
    AuthUser u;
    u.user_id = r[0]["user_id"].as<i64>();
    u.username = r[0]["username"].as<std::string>();
    u.password_hash = r[0]["password_hash"].as<std::string>();
    u.type = r[0]["user_type"].as<std::string>();
    u.name = r[0]["name"].as<std::string>();
    u.is_active = r[0]["is_active"].as<bool>();
    u.must_change_password = r[0]["must_change_password"].as<bool>();
    return u;
}

void Engine::create_session(i64 user_id, const std::string& token_hex, int ttl_seconds) {
    transact(*impl_, [&](pqxx::work& tx) {
        tx.exec_params0("INSERT INTO session_token(token, user_id, expires_at) VALUES ($1, $2, now() + $3::int * interval '1 second')",
                        token_hex, user_id, ttl_seconds);
    });
}

std::optional<SessionInfo> Engine::find_session(const std::string& token_hex) {
    ReadTx rt(*impl_);
    const pqxx::result r = rt.tx.exec_params(
        "SELECT u.user_id, u.username, u.user_type, u.must_change_password, "
        "coalesce(m.manager_name, s.seller_name, b.buyer_name) AS name "
        "FROM session_token t JOIN app_user u ON u.user_id = t.user_id "
        "LEFT JOIN pool_manager m ON m.user_id = u.user_id LEFT JOIN seller s ON s.seller_id = u.user_id "
        "LEFT JOIN buyer b ON b.buyer_id = u.user_id "
        "WHERE t.token = $1 AND t.expires_at > now() AND u.is_active", token_hex);
    if (r.empty()) return std::nullopt;
    SessionInfo s;
    s.user_id = r[0]["user_id"].as<i64>();
    s.username = r[0]["username"].as<std::string>();
    s.type = r[0]["user_type"].as<std::string>();
    s.name = r[0]["name"].as<std::string>();
    s.must_change_password = r[0]["must_change_password"].as<bool>();
    return s;
}

void Engine::delete_session(const std::string& token_hex) {
    transact(*impl_, [&](pqxx::work& tx) { tx.exec_params0("DELETE FROM session_token WHERE token = $1", token_hex); });
}

void Engine::delete_other_sessions(i64 user_id, const std::string& keep_token_hex) {
    transact(*impl_, [&](pqxx::work& tx) {
        tx.exec_params0("DELETE FROM session_token WHERE user_id = $1 AND token <> $2", user_id, keep_token_hex);
    });
}

void Engine::set_password(i64 user_id, const std::string& new_password_hash) {
    if (new_password_hash.empty()) throw DomainError("INVALID", "password hash is missing");
    transact(*impl_, [&](pqxx::work& tx) {
        tx.exec_params0("UPDATE app_user SET password_hash = $2, must_change_password = FALSE WHERE user_id = $1", user_id, new_password_hash);
    });
}

json Engine::list_users() {
    ReadTx rt(*impl_);
    json out = json::array();
    for (const auto& r : rt.tx.exec(
             "SELECT u.user_id, u.username, u.user_type, u.is_active, u.must_change_password, "
             "coalesce(m.manager_name, s.seller_name, b.buyer_name) AS name "
             "FROM app_user u LEFT JOIN pool_manager m ON m.user_id = u.user_id LEFT JOIN seller s ON s.seller_id = u.user_id "
             "LEFT JOIN buyer b ON b.buyer_id = u.user_id ORDER BY u.user_id"))
        out.push_back({{"user_id", r["user_id"].as<i64>()}, {"name", r["name"].as<std::string>()},
                       {"username", r["username"].as<std::string>()}, {"type", r["user_type"].as<std::string>()},
                       {"status", !r["is_active"].as<bool>() ? "inactive" : r["must_change_password"].as<bool>() ? "waiting_first_login" : "active"}});
    return out;
}

// ---- reference data --------------------------------------------------------------------------------------------------

json Engine::commodities() {
    ReadTx rt(*impl_);
    json out = json::array();
    std::map<i64, std::size_t> at;
    for (const auto& r : rt.tx.exec("SELECT commodity_id, name FROM commodity ORDER BY commodity_id")) {
        at[r[0].as<i64>()] = out.size();
        out.push_back({{"commodity_id", r[0].as<i64>()}, {"name", r[1].as<std::string>()}, {"parameters", json::array()}});
    }
    for (const auto& r : rt.tx.exec(
             "SELECT commodity_id, parameter_name, unit, range1_max::text AS r1, range2_max::text AS r2, range3_max::text AS r3, "
             "verified, source, source_date::text AS source_date FROM commodity_parameter ORDER BY commodity_id, ctid"))   // limits in the order they were entered (Moisture first), as the wireframes show them
        out[at.at(r["commodity_id"].as<i64>())]["parameters"].push_back(
            {{"name", r["parameter_name"].as<std::string>()}, {"unit", r["unit"].as<std::string>()},
             {"range1_max", opt_str(r["r1"])}, {"range2_max", opt_str(r["r2"])}, {"range3_max", opt_str(r["r3"])},
             {"verified", r["verified"].as<bool>()}, {"source", opt_str(r["source"])}, {"source_date", opt_str(r["source_date"])}});
    return out;
}

json Engine::reference_prices() {
    ReadTx rt(*impl_);
    json out = json::array();
    for (const auto& r : rt.tx.exec(
             "SELECT DISTINCT ON (commodity_id, kind) commodity_id, kind, price_paise_per_quintal, label, as_of::text AS as_of "
             "FROM reference_price ORDER BY commodity_id, kind, as_of DESC, ref_id DESC"))
        out.push_back({{"commodity_id", r["commodity_id"].as<i64>()}, {"kind", r["kind"].as<std::string>()},
                       {"price_paise_per_quintal", r["price_paise_per_quintal"].as<i64>()},
                       {"label", r["label"].as<std::string>()}, {"as_of", r["as_of"].as<std::string>()}});
    return out;
}

// ---- pools -----------------------------------------------------------------------------------------------------------

json Engine::list_pools(const Viewer& v) {
    ReadTx rt(*impl_);
    pqxx::result rows;
    const std::string buyer_rule = std::string("p.status = 'open' AND (p.total_stock_g > 0 OR ") + kSoldRecently + ")";
    if (v.role == Viewer::Role::Manager)
        rows = rt.tx.exec(std::string(kPoolSelect) + "ORDER BY p.pool_id DESC");
    else if (v.role == Viewer::Role::Buyer)
        rows = rt.tx.exec(std::string(kPoolSelect) + "WHERE " + buyer_rule + " ORDER BY p.pool_id DESC");
    else
        rows = rt.tx.exec_params(std::string(kPoolSelect) + "WHERE (" + buyer_rule +
                                 ") OR (p.status <> 'draft' AND EXISTS (SELECT 1 FROM pool_seller_balance b WHERE b.pool_id = p.pool_id AND b.seller_id = $1)) "
                                 "ORDER BY p.pool_id DESC", v.user_id);

    // newest three photos per pool, in one query
    std::map<i64, json> photos;
    for (const auto& r : rt.tx.exec(
             "SELECT pool_id, photo_id, file_path FROM (SELECT pool_id, photo_id, file_path, "
             "row_number() OVER (PARTITION BY pool_id ORDER BY photo_id DESC) AS rn FROM pool_photo) x WHERE rn <= 3 ORDER BY pool_id, rn")) {
        json& arr = photos[r[0].as<i64>()];
        if (arr.is_null()) arr = json::array();
        arr.push_back({{"photo_id", r[1].as<i64>()}, {"url", "/" + r[2].as<std::string>()}});
    }

    json out = json::array();
    for (const auto& r : rows) {
        json j = pool_common(r);
        const i64 id = j["pool_id"].get<i64>();
        j["photos"] = photos.count(id) ? photos[id] : json::array();
        if (v.role == Viewer::Role::Manager) {
            if (r["entries"].as<i64>() <= 1) j["records"] = "none";   // nothing but the pool's creation yet
            else j["records"] = e1::check_invariants(load_pool_facts(rt.tx, id, std::nullopt)).empty() ? "ok" : "mismatch";
        }
        out.push_back(std::move(j));
    }
    return out;
}

static json price_and_status_changes(pqxx::transaction_base& tx, i64 pool_id, int limit);

json Engine::pool_detail(i64 pool_id, const Viewer& v) {
    ReadTx rt(*impl_);
    const pqxx::result r = rt.tx.exec_params(std::string(kPoolSelect) + "WHERE p.pool_id = $1", pool_id);
    if (r.empty()) throw DomainError("NOT_FOUND", "pool not found");
    const std::string status = r[0]["status"].as<std::string>();
    if (v.role != Viewer::Role::Manager) {
        const bool mine = v.role == Viewer::Role::Farmer && holds_share(rt.tx, pool_id, v.user_id);
        const bool visible = status == "open" || (mine && status == "closed");
        if (!visible) throw DomainError("NOT_FOUND", "pool not found");
    }

    json j = pool_common(r[0]);
    const i64 stock = j["stock_g"].get<i64>();

    // the Range's quality limits (only the numbers that exist), and the reference lines
    const i64 commodity_id = j["commodity_id"].get<i64>();
    json limits = json::array();
    bool all_verified = true;
    for (const auto& p : rt.tx.exec_params(
             "SELECT parameter_name, unit, CASE $2::int WHEN 1 THEN range1_max WHEN 2 THEN range2_max ELSE range3_max END::text AS lim, verified "
             "FROM commodity_parameter WHERE commodity_id = $1 ORDER BY ctid", commodity_id, j["range_no"].get<int>())) {
        if (p["lim"].is_null()) continue;
        limits.push_back({{"name", p["parameter_name"].as<std::string>()}, {"unit", p["unit"].as<std::string>()}, {"max", p["lim"].as<std::string>()}});
        all_verified = all_verified && p["verified"].as<bool>();
    }
    j["limits"] = limits;
    j["limits_verified"] = !limits.empty() && all_verified;
    j["reference"] = {{"msp", nullptr}, {"mandi_modal", nullptr}};
    for (const auto& p : rt.tx.exec_params(
             "SELECT DISTINCT ON (kind) kind, price_paise_per_quintal, label, as_of::text AS as_of FROM reference_price "
             "WHERE commodity_id = $1 ORDER BY kind, as_of DESC, ref_id DESC", commodity_id))
        j["reference"][p["kind"].as<std::string>() == "MSP" ? "msp" : "mandi_modal"] =
            {{"price_paise_per_quintal", p["price_paise_per_quintal"].as<i64>()}, {"label", p["label"].as<std::string>()}, {"as_of", p["as_of"].as<std::string>()}};

    j["photos"] = photo_array(rt.tx, pool_id, v.role == Viewer::Role::Manager ? 100 : 3);
    {   // price history is visible to everyone who can see the pool (status changes are not needed by buyers)
        json history = json::array();
        for (const auto& c : price_and_status_changes(rt.tx, pool_id, 50))
            if (c["field"] == "PRICE") history.push_back(c);
        j["price_history"] = history;
    }
    j["records_ok"] = e1::check_invariants(load_pool_facts(rt.tx, pool_id, std::nullopt)).empty();   // status only for everyone

    const i64 deposited = rt.tx.exec_params1("SELECT coalesce(sum(quantity_g),0)::bigint FROM contribution WHERE pool_id = $1", pool_id)[0].as<i64>();
    const i64 sold = rt.tx.exec_params1("SELECT coalesce(sum(quantity_g),0)::bigint FROM sale WHERE pool_id = $1", pool_id)[0].as<i64>();
    if (v.role != Viewer::Role::Buyer) { j["deposited_g"] = deposited; j["sold_g"] = sold; }   // buyers do not need the pool's history

    if (v.role == Viewer::Role::Manager) {
        json farmers = json::array();
        for (const auto& f : rt.tx.exec_params(
                 "SELECT b.seller_id, s.seller_name, b.remaining_g FROM pool_seller_balance b JOIN seller s ON s.seller_id = b.seller_id "
                 "WHERE b.pool_id = $1 ORDER BY b.seller_id", pool_id))
            farmers.push_back({{"seller_id", f[0].as<i64>()}, {"name", f[1].as<std::string>()}, {"remaining_g", f[2].as<i64>()},
                               {"share_permille", permille(f[2].as<i64>(), stock)}});
        j["farmers"] = farmers;
    } else if (v.role == Viewer::Role::Farmer) {
        const pqxx::result b = rt.tx.exec_params("SELECT remaining_g FROM pool_seller_balance WHERE pool_id = $1 AND seller_id = $2", pool_id, v.user_id);
        if (!b.empty()) {
            const i64 mine = b[0][0].as<i64>();
            const i64 my_dep = rt.tx.exec_params1("SELECT coalesce(sum(quantity_g),0)::bigint FROM contribution WHERE pool_id = $1 AND seller_id = $2", pool_id, v.user_id)[0].as<i64>();
            const i64 paid = rt.tx.exec_params1("SELECT coalesce(sum(amount_paise),0)::bigint FROM financial_ledger WHERE pool_id = $1 AND seller_id = $2 AND entry_type = 'SELLER_PAYOUT'", pool_id, v.user_id)[0].as<i64>();
            j["my_share"] = {{"remaining_g", mine}, {"share_permille", permille(mine, stock)}, {"deposited_g", my_dep}, {"paid_paise", paid}};
        }
    }
    return j;
}

json Engine::pool_photos(i64 pool_id, const Viewer& v) {
    ReadTx rt(*impl_);
    const pqxx::result r = rt.tx.exec_params("SELECT status FROM pool WHERE pool_id = $1", pool_id);
    if (r.empty()) throw DomainError("NOT_FOUND", "pool not found");
    const std::string status = r[0][0].as<std::string>();
    if (v.role != Viewer::Role::Manager) {
        const bool mine = v.role == Viewer::Role::Farmer && holds_share(rt.tx, pool_id, v.user_id);
        if (!(status == "open" || (mine && status == "closed"))) throw DomainError("NOT_FOUND", "pool not found");
    }
    return photo_array(rt.tx, pool_id, v.role == Viewer::Role::Manager ? 100 : 3);
}

json Engine::ledger_page(i64 pool_id, const std::string& ledger, std::optional<i64> before_id, int limit) {
    if (ledger != "inventory" && ledger != "ownership" && ledger != "financial") throw DomainError("INVALID", "unknown ledger");
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;
    ReadTx rt(*impl_);
    if (rt.tx.exec_params("SELECT 1 FROM pool WHERE pool_id = $1", pool_id).empty()) throw DomainError("NOT_FOUND", "pool not found");

    json out = {{"ledger", ledger}, {"rows", json::array()}, {"next_before_id", nullptr}};
    pqxx::result rows;
    if (ledger == "inventory")
        rows = rt.tx.exec_params("SELECT inv_id AS id, event_id, movement_type, delta_g, resulting_stock_g, " + secs("created_at") +
                                 " AS at FROM inventory_ledger WHERE pool_id = $1 AND ($2::bigint IS NULL OR inv_id < $2) ORDER BY inv_id DESC LIMIT $3",
                                 pool_id, before_id, static_cast<i64>(limit));
    else if (ledger == "ownership")
        rows = rt.tx.exec_params("SELECT o.own_id AS id, o.event_id, o.seller_id, s.seller_name, o.delta_g, o.resulting_balance_g, " + secs("o.created_at") +
                                 " AS at FROM ownership_ledger o JOIN seller s ON s.seller_id = o.seller_id WHERE o.pool_id = $1 AND ($2::bigint IS NULL OR o.own_id < $2) "
                                 "ORDER BY o.own_id DESC LIMIT $3", pool_id, before_id, static_cast<i64>(limit));
    else
        rows = rt.tx.exec_params("SELECT f.fin_id AS id, f.event_id, f.entry_type, f.seller_id, s.seller_name, f.amount_paise, " + secs("f.created_at") +
                                 " AS at FROM financial_ledger f LEFT JOIN seller s ON s.seller_id = f.seller_id WHERE f.pool_id = $1 AND ($2::bigint IS NULL OR f.fin_id < $2) "
                                 "ORDER BY f.fin_id DESC LIMIT $3", pool_id, before_id, static_cast<i64>(limit));
    for (const auto& r : rows) {
        json j = {{"id", r["id"].as<i64>()}, {"event_id", r["event_id"].as<i64>()}, {"at", r["at"].as<std::string>()}};
        if (ledger == "inventory") { j["movement_type"] = r["movement_type"].as<std::string>(); j["delta_g"] = r["delta_g"].as<i64>(); j["resulting_stock_g"] = r["resulting_stock_g"].as<i64>(); }
        else if (ledger == "ownership") { j["seller_id"] = r["seller_id"].as<i64>(); j["seller_name"] = r["seller_name"].as<std::string>(); j["delta_g"] = r["delta_g"].as<i64>(); j["resulting_balance_g"] = r["resulting_balance_g"].as<i64>(); }
        else { j["entry_type"] = r["entry_type"].as<std::string>(); j["seller_id"] = opt_i64(r["seller_id"]); j["seller_name"] = opt_str(r["seller_name"]); j["amount_paise"] = r["amount_paise"].as<i64>(); }
        out["rows"].push_back(std::move(j));
    }
    if (static_cast<int>(rows.size()) == limit) out["next_before_id"] = out["rows"].back()["id"];

    if (ledger == "inventory" && !before_id) {
        // price and status changes appear in the stock feed as different rows; they are not ledger rows
        out["changes"] = json::array();
        for (const auto& c : rt.tx.exec_params(
                 "SELECT c.change_id, c.event_id, c.field, c.old_value, c.new_value, " + secs("e.created_at") +
                 " AS at FROM pool_change c JOIN event e ON e.event_id = c.event_id WHERE c.pool_id = $1 ORDER BY c.change_id DESC LIMIT 200", pool_id))
            out["changes"].push_back({{"kind", "change"}, {"id", c["change_id"].as<i64>()}, {"event_id", c["event_id"].as<i64>()},
                                      {"field", c["field"].as<std::string>()}, {"old_value", c["old_value"].as<std::string>()},
                                      {"new_value", c["new_value"].as<std::string>()}, {"at", c["at"].as<std::string>()}});
    }
    return out;
}

// ---- farmer views ----------------------------------------------------------------------------------------------------

json Engine::my_pools(i64 farmer_id) {
    ReadTx rt(*impl_);
    json out = json::array();
    for (const auto& r : rt.tx.exec_params(
             "SELECT p.pool_id, c.name AS commodity, p.variety, p.range_no, p.status, p.total_stock_g, p.price_paise_per_kg, b.remaining_g "
             "FROM pool_seller_balance b JOIN pool p ON p.pool_id = b.pool_id JOIN commodity c ON c.commodity_id = p.commodity_id "
             "WHERE b.seller_id = $1 AND p.status <> 'draft' ORDER BY p.pool_id DESC", farmer_id)) {
        const i64 pool_id = r["pool_id"].as<i64>();
        const i64 stock = r["total_stock_g"].as<i64>(), left = r["remaining_g"].as<i64>();
        json j = {{"pool_id", pool_id}, {"commodity", r["commodity"].as<std::string>()}, {"variety", r["variety"].as<std::string>()},
                  {"range_no", r["range_no"].as<int>()}, {"status", r["status"].as<std::string>()}, {"stock_g", stock},
                  {"price_paise_per_kg", r["price_paise_per_kg"].as<i64>()}, {"my_remaining_g", left}, {"my_share_permille", permille(left, stock)}};
        j["my_deposited_g"] = rt.tx.exec_params1("SELECT coalesce(sum(quantity_g),0)::bigint FROM contribution WHERE pool_id = $1 AND seller_id = $2", pool_id, farmer_id)[0].as<i64>();
        j["paid_to_me_paise"] = rt.tx.exec_params1("SELECT coalesce(sum(amount_paise),0)::bigint FROM financial_ledger WHERE pool_id = $1 AND seller_id = $2 AND entry_type = 'SELLER_PAYOUT'", pool_id, farmer_id)[0].as<i64>();
        j["activity"] = json::array();
        for (const auto& a : rt.tx.exec_params(
                 "SELECT e.event_type, " + secs("e.created_at") + " AS at, o.delta_g, o.resulting_balance_g, s.quantity_g AS sale_g, f.amount_paise "
                 "FROM ownership_ledger o JOIN event e ON e.event_id = o.event_id LEFT JOIN sale s ON s.event_id = o.event_id "
                 "LEFT JOIN financial_ledger f ON f.event_id = o.event_id AND f.seller_id = o.seller_id AND f.entry_type = 'SELLER_PAYOUT' "
                 "WHERE o.pool_id = $1 AND o.seller_id = $2 ORDER BY o.own_id DESC LIMIT 5", pool_id, farmer_id))
            j["activity"].push_back({{"at", a["at"].as<std::string>()}, {"kind", a["event_type"].as<std::string>() == "SALE" ? "sale" : "deposit"},
                                     {"my_delta_g", a["delta_g"].as<i64>()}, {"left_g", a["resulting_balance_g"].as<i64>()},
                                     {"pool_sale_g", opt_i64(a["sale_g"])}, {"paise", opt_i64(a["amount_paise"])}});
        out.push_back(std::move(j));
    }
    return out;
}

// Price and status changes of a pool, newest first: what changed, from what to what, and when.
static json price_and_status_changes(pqxx::transaction_base& tx, i64 pool_id, int limit) {
    json out = json::array();
    for (const auto& c : tx.exec_params("SELECT c.field, c.old_value, c.new_value, " + secs("e.created_at") +
                                        " AS at FROM pool_change c JOIN event e ON e.event_id = c.event_id WHERE c.pool_id = $1 ORDER BY c.change_id DESC LIMIT $2", pool_id, limit))
        out.push_back({{"field", c["field"].as<std::string>()}, {"old_value", c["old_value"].as<std::string>()}, {"new_value", c["new_value"].as<std::string>()}, {"at", c["at"].as<std::string>()}});
    return out;
}

json Engine::farmer_statement(i64 farmer_id, i64 pool_id) {
    ReadTx rt(*impl_);
    const pqxx::result p = rt.tx.exec_params(
        "SELECT c.name AS commodity, p.variety, p.range_no, p.status, p.total_stock_g, p.price_paise_per_kg FROM pool p "
        "JOIN commodity c ON c.commodity_id = p.commodity_id WHERE p.pool_id = $1", pool_id);
    if (p.empty()) throw DomainError("NOT_FOUND", "pool not found");
    const i64 stock = p[0]["total_stock_g"].as<i64>();
    const pqxx::result bal = rt.tx.exec_params("SELECT remaining_g FROM pool_seller_balance WHERE pool_id = $1 AND seller_id = $2", pool_id, farmer_id);
    if (bal.empty()) throw DomainError("NOT_FOUND", "this farmer has no share in the pool");
    const i64 left = bal[0][0].as<i64>();

    const std::string farmer_name = rt.tx.exec_params1("SELECT seller_name FROM seller WHERE seller_id = $1", farmer_id)[0].as<std::string>();
    json out = {{"farmer_name", farmer_name}, {"pool_id", pool_id}, {"commodity", p[0]["commodity"].as<std::string>()}, {"variety", p[0]["variety"].as<std::string>()},
                {"range_no", p[0]["range_no"].as<int>()}, {"status", p[0]["status"].as<std::string>()}, {"stock_g", stock},
                {"price_paise_per_kg", p[0]["price_paise_per_kg"].as<i64>()}};
    const i64 deposited = rt.tx.exec_params1("SELECT coalesce(sum(quantity_g),0)::bigint FROM contribution WHERE pool_id = $1 AND seller_id = $2", pool_id, farmer_id)[0].as<i64>();
    const i64 paid = rt.tx.exec_params1("SELECT coalesce(sum(amount_paise),0)::bigint FROM financial_ledger WHERE pool_id = $1 AND seller_id = $2 AND entry_type = 'SELLER_PAYOUT'", pool_id, farmer_id)[0].as<i64>();
    out["summary"] = {{"deposited_g", deposited}, {"sold_g", deposited - left}, {"paid_paise", paid}, {"left_g", left}, {"share_permille", permille(left, stock)}};
    out["records_ok"] = e1::check_invariants(load_pool_facts(rt.tx, pool_id, std::nullopt)).empty();
    out["rows"] = json::array();
    for (const auto& a : rt.tx.exec_params(
             "SELECT e.event_type, " + secs("e.created_at") + " AS at, o.delta_g, o.resulting_balance_g, s.quantity_g AS sale_g, "
             "s.unit_price_paise_per_kg AS unit_price, s.sale_id, s.gross_paise, b.buyer_name, l.entry_hash, f.amount_paise "
             "FROM ownership_ledger o JOIN event e ON e.event_id = o.event_id LEFT JOIN sale s ON s.event_id = o.event_id "
             "LEFT JOIN buyer b ON b.buyer_id = s.buyer_id LEFT JOIN ledger_entry l ON l.event_id = o.event_id "
             "LEFT JOIN financial_ledger f ON f.event_id = o.event_id AND f.seller_id = o.seller_id AND f.entry_type = 'SELLER_PAYOUT' "
             "WHERE o.pool_id = $1 AND o.seller_id = $2 ORDER BY o.own_id DESC", pool_id, farmer_id)) {
        const bool sale = a["event_type"].as<std::string>() == "SALE";
        json row = {{"at", a["at"].as<std::string>()}, {"kind", sale ? "sale" : "deposit"}, {"delta_g", a["delta_g"].as<i64>()},
                    {"left_g", a["resulting_balance_g"].as<i64>()}, {"paise", opt_i64(a["amount_paise"])},
                    {"pool_sale_g", opt_i64(a["sale_g"])}, {"unit_price_paise_per_kg", opt_i64(a["unit_price"])}};
        row["share_permille"] = sale ? json(permille(-a["delta_g"].as<i64>(), a["sale_g"].as<i64>())) : json(nullptr);   // this farmer's part of that sale
        // the farmer sees who bought and the receipt; buyers never receive farmer names (the other direction stays closed)
        char no[32];
        if (sale) std::snprintf(no, sizeof no, "R-%04lld", static_cast<long long>(a["sale_id"].as<i64>()));
        row["receipt_no"] = sale ? json(std::string(no)) : json(nullptr);
        row["buyer_name"] = sale ? json(a["buyer_name"].as<std::string>()) : json(nullptr);
        row["gross_paise"] = sale ? json(a["gross_paise"].as<i64>()) : json(nullptr);
        row["seal"] = a["entry_hash"].is_null() ? json(nullptr) : json(a["entry_hash"].as<std::string>().substr(0, 8));   // first 8 hex digits of the record's hash
        out["rows"].push_back(std::move(row));
    }
    out["changes"] = price_and_status_changes(rt.tx, pool_id, 20);
    return out;
}

// ---- buyer view ------------------------------------------------------------------------------------------------------

json Engine::my_purchases(i64 buyer_id) {
    ReadTx rt(*impl_);
    json out = json::array();
    for (const auto& r : rt.tx.exec_params(
             "SELECT s.sale_id, " + secs("e.created_at") + " AS at, s.pool_id, c.name AS commodity, p.variety, p.range_no, s.quantity_g, "
             "s.unit_price_paise_per_kg, s.gross_paise, p.storage_location FROM sale s JOIN event e ON e.event_id = s.event_id "
             "JOIN pool p ON p.pool_id = s.pool_id JOIN commodity c ON c.commodity_id = p.commodity_id "
             "WHERE s.buyer_id = $1 ORDER BY s.sale_id DESC", buyer_id)) {
        char no[32];
        std::snprintf(no, sizeof no, "R-%04lld", static_cast<long long>(r["sale_id"].as<i64>()));
        out.push_back({{"sale_id", r["sale_id"].as<i64>()}, {"receipt_no", no}, {"at", r["at"].as<std::string>()}, {"pool_id", r["pool_id"].as<i64>()},
                       {"commodity", r["commodity"].as<std::string>()}, {"variety", r["variety"].as<std::string>()}, {"range_no", r["range_no"].as<int>()},
                       {"quantity_g", r["quantity_g"].as<i64>()}, {"unit_price_paise_per_kg", r["unit_price_paise_per_kg"].as<i64>()},
                       {"gross_paise", r["gross_paise"].as<i64>()}, {"storage_location", r["storage_location"].as<std::string>()}});
    }
    return out;
}

// ---- live feed -------------------------------------------------------------------------------------------------------

json Engine::event_view(i64 event_id) {
    ReadTx rt(*impl_);
    const pqxx::result e = rt.tx.exec_params(
        "SELECT e.event_id, e.pool_id, e.event_type, e.actor_user_id, " + secs("e.created_at") + " AS at, "
        "p.total_stock_g, p.price_paise_per_kg, p.status FROM event e JOIN pool p ON p.pool_id = e.pool_id WHERE e.event_id = $1", event_id);
    if (e.empty()) return nullptr;
    json j = {{"event_id", event_id}, {"pool_id", e[0]["pool_id"].as<i64>()}, {"event_type", e[0]["event_type"].as<std::string>()},
              {"at", e[0]["at"].as<std::string>()}, {"stock_g", e[0]["total_stock_g"].as<i64>()},
              {"price_paise_per_kg", e[0]["price_paise_per_kg"].as<i64>()}, {"status", e[0]["status"].as<std::string>()}};
    j["inventory"] = json::array();
    for (const auto& r : rt.tx.exec_params("SELECT inv_id, movement_type, delta_g, resulting_stock_g, " + secs("created_at") + " FROM inventory_ledger WHERE event_id = $1 ORDER BY inv_id", event_id))
        j["inventory"].push_back({{"id", r[0].as<i64>()}, {"event_id", event_id}, {"movement_type", r[1].as<std::string>()}, {"delta_g", r[2].as<i64>()}, {"resulting_stock_g", r[3].as<i64>()}, {"at", r[4].as<std::string>()}});
    j["ownership"] = json::array();
    for (const auto& r : rt.tx.exec_params("SELECT o.own_id, o.seller_id, s.seller_name, o.delta_g, o.resulting_balance_g, " + secs("o.created_at") +
                                           " FROM ownership_ledger o JOIN seller s ON s.seller_id = o.seller_id WHERE o.event_id = $1 ORDER BY o.own_id", event_id))
        j["ownership"].push_back({{"id", r[0].as<i64>()}, {"event_id", event_id}, {"seller_id", r[1].as<i64>()}, {"seller_name", r[2].as<std::string>()}, {"delta_g", r[3].as<i64>()}, {"resulting_balance_g", r[4].as<i64>()}, {"at", r[5].as<std::string>()}});
    j["financial"] = json::array();
    for (const auto& r : rt.tx.exec_params("SELECT f.fin_id, f.entry_type, f.seller_id, s.seller_name, f.amount_paise, " + secs("f.created_at") +
                                           " FROM financial_ledger f LEFT JOIN seller s ON s.seller_id = f.seller_id WHERE f.event_id = $1 ORDER BY f.fin_id", event_id))
        j["financial"].push_back({{"id", r[0].as<i64>()}, {"event_id", event_id}, {"entry_type", r[1].as<std::string>()}, {"seller_id", opt_i64(r[2])}, {"seller_name", opt_str(r[3])}, {"amount_paise", r[4].as<i64>()}, {"at", r[5].as<std::string>()}});
    j["change"] = nullptr;
    for (const auto& r : rt.tx.exec_params("SELECT change_id, field, old_value, new_value FROM pool_change WHERE event_id = $1", event_id))
        j["change"] = {{"kind", "change"}, {"id", r[0].as<i64>()}, {"event_id", event_id}, {"field", r[1].as<std::string>()}, {"old_value", r[2].as<std::string>()}, {"new_value", r[3].as<std::string>()}, {"at", j["at"]}};
    j["sale"] = nullptr;
    for (const auto& r : rt.tx.exec_params("SELECT sale_id, buyer_id, quantity_g, unit_price_paise_per_kg, gross_paise FROM sale WHERE event_id = $1", event_id)) {
        char no[32];
        std::snprintf(no, sizeof no, "R-%04lld", static_cast<long long>(r[0].as<i64>()));
        j["sale"] = {{"sale_id", r[0].as<i64>()}, {"receipt_no", no}, {"buyer_id", r[1].as<i64>()}, {"quantity_g", r[2].as<i64>()},
                     {"unit_price_paise_per_kg", r[3].as<i64>()}, {"gross_paise", r[4].as<i64>()}, {"created_at", j["at"]}};
    }
    return j;
}

}  // namespace e2
