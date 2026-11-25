// E4 integration tests: a real server (in this process, free port) over HTTP and WebSocket, against the real database.
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <thread>

#include "db_fixture.hpp"
#include "e4/server.hpp"
#include "http_client.hpp"

using namespace testdb;
using namespace std::chrono_literals;
using testhttp::Client;
using testhttp::Response;
using testhttp::WsClient;
using json = nlohmann::json;

namespace {

const std::string kTemp = "Temp-pass-1234";
const std::string kPass = "Sup3rSecret-pass";
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, 'x');
constexpr int kM = 1, kF = 2, kB = 4, kAny = 7;

struct Person {
    i64 id = 0;
    std::string username, name, type;
};

bool has_key(const json& j, const std::set<std::string>& keys) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it)
            if (keys.count(it.key()) || has_key(it.value(), keys)) return true;
    } else if (j.is_array()) {
        for (const json& e : j) if (has_key(e, keys)) return true;
    }
    return false;
}

std::vector<json> of_type(const std::vector<json>& msgs, const std::string& type) {
    std::vector<json> out;
    for (const json& m : msgs) if (m.value("type", "") == type) out.push_back(m);
    return out;
}

std::set<std::string> ledgers_of(const std::vector<json>& msgs) {
    std::set<std::string> out;
    for (const json& m : of_type(msgs, "ledger_rows")) out.insert(m["ledger"].get<std::string>());
    return out;
}

}  // namespace

class E4Test : public DbTest {
protected:
    static inline std::unique_ptr<e4::Server> server;
    static inline std::string photo_dir;
    static inline std::atomic<bool> feed_down{false};
    static inline json feed_records = json::array();

    static void SetUpTestSuite() {
        if (app_conninfo().empty()) return;
        photo_dir = (std::filesystem::temp_directory_path() / unique_name("agri-photos")).string();
        e4::Config c;
        c.db_conninfo = app_conninfo();
        c.port = 0;
        c.photo_dir = photo_dir;
        c.web_dir = AGRI_WEB_DIR;
        c.http_threads = 6;
        c.db_pool_size = 10;
        c.lock_timeout_ms = 300;
        c.queue = e3::Options{4, 3, 5ms, 10000ms, 1000};
        c.pwhash_opslimit = 1;     // the cheapest argon2id setting so tests stay fast; production uses libsodium's defaults
        c.pwhash_memlimit = 8192;
        c.max_photo_bytes = 100 * 1024;
        c.signup_max_attempts_per_hour = 500;   // the sign-up throttle has its own test with its own server
        c.market_min_seconds_between_updates = 0;   // the "too soon" rule has its own test with its own server
        c.market_feed = [](const std::string&, int offset, int) -> std::string {   // a fake price file: tests never use the internet
            if (feed_down) throw std::runtime_error("the price service is down");
            const json records = offset == 0 ? feed_records : json::array();
            return json{{"total", feed_records.size()}, {"count", records.size()}, {"records", records}}.dump();
        };
        server = std::make_unique<e4::Server>(c);
        server->start();
    }
    static void TearDownTestSuite() {
        if (server) { server->stop(); server.reset(); }
        std::error_code ec;
        std::filesystem::remove_all(photo_dir, ec);
    }

    std::uint16_t port() const { return server->port(); }
    Client anon() const { return Client(port()); }

    // ---- people ----------------------------------------------------------------------------------------------------
    Person manager;
    Client mgr{0};
    Person farmer[3];
    Client farmer_c[3] = {Client(0), Client(0), Client(0)};
    Person buyer[2];
    Client buyer_c[2] = {Client(0), Client(0)};

    // First login with the temporary password, the forced change, session kept.
    Client activate(const Person& p, const std::string& temp) {
        Client c(port());
        const Response r = c.login(p.username, temp);
        EXPECT_EQ(r.status, 200) << p.username;
        EXPECT_TRUE(r.j()["must_change_password"].get<bool>());
        const Response ch = c.post("/auth/change-password", {{"current_password", temp}, {"new_password", kPass}});
        EXPECT_EQ(ch.status, 200) << ch.body;
        return c;
    }
    Client session_of(const Person& p) const {
        Client c(port());
        const Response r = c.login(p.username, kPass);
        EXPECT_EQ(r.status, 200) << p.username << " " << r.body;
        return c;
    }
    Person make_user(Client& as_manager, const std::string& type, const std::string& name) {
        Person p;
        p.username = unique_name(type == "FARMER" ? "farmer" : "buyer");
        p.name = name;
        p.type = type;
        const Response r = as_manager.post("/users", {{"name", name}, {"username", p.username}, {"type", type}, {"temporary_password", kTemp}});
        EXPECT_EQ(r.status, 201) << r.body;
        p.id = r.j()["user_id"].get<i64>();
        return p;
    }
    void build_world() {
        manager.username = unique_name("mgr");
        manager.name = "Pool Manager";
        manager.type = "MANAGER";
        manager.id = server->create_account(manager.name, manager.username, "MANAGER", kTemp);
        mgr = activate(manager, kTemp);
        const char* names[3] = {"Zorbax Farmer", "Quillon Farmer", "Wexley Farmer"};
        for (int i = 0; i < 3; ++i) { farmer[i] = make_user(mgr, "FARMER", names[i]); farmer_c[i] = activate(farmer[i], kTemp); }
        for (int i = 0; i < 2; ++i) { buyer[i] = make_user(mgr, "BUYER", std::string("Buyer ") + char('1' + i)); buyer_c[i] = activate(buyer[i], kTemp); }
    }

    // ---- pools -----------------------------------------------------------------------------------------------------
    i64 paddy_id() {
        for (const json& c : mgr.get("/reference/commodities").j()) if (c["name"] == "Paddy") return c["commodity_id"].get<i64>();
        return 0;
    }
    i64 make_draft_pool(i64 price = 2500, i64 min_order = 100000) {
        const Response r = mgr.post("/pools", {{"commodity_id", paddy_id()}, {"variety", "Sona Masuri"}, {"range_no", 1}, {"packaging", "50 kg jute bags"},
                                               {"storage_location", "Test godown"}, {"min_order_g", min_order}, {"price_paise_per_kg", price}});
        EXPECT_EQ(r.status, 201) << r.body;
        return r.j()["pool_id"].get<i64>();
    }
    Response upload(Client& c, i64 pool, const std::string& bytes, const std::string& filename = "lot.png", const std::string& type = "image/png") {
        return c.post_raw("/pools/" + std::to_string(pool) + "/photos", testhttp::multipart_body("agriBoundary7", "photo", filename, type, bytes),
                          "multipart/form-data; boundary=agriBoundary7");
    }
    i64 make_open_pool(i64 price = 2500, i64 min_order = 100000) {
        const i64 pool = make_draft_pool(price, min_order);
        EXPECT_EQ(upload(mgr, pool, kPng).status, 201);
        EXPECT_EQ(mgr.patch("/pools/" + std::to_string(pool), {{"status", "open"}}).status, 200);
        return pool;
    }
    void deposit(i64 pool, const Person& f, i64 grams) {
        const Response r = mgr.post("/pools/" + std::to_string(pool) + "/contributions", {{"seller_id", f.id}, {"quantity_g", grams}});
        EXPECT_EQ(r.status, 201) << r.body;
    }
    i64 make_worked_example_pool() {   // 600 / 800 / 600 kg at 2,500 paise per kg
        const i64 pool = make_open_pool();
        deposit(pool, farmer[0], 600000);
        deposit(pool, farmer[1], 800000);
        deposit(pool, farmer[2], 600000);
        return pool;
    }
    Response sale(Client& c, i64 pool, i64 qty, i64 price = 2500, const json& extra = json::object(), const std::string& request_id = new_uuid()) {
        json body = {{"request_id", request_id}, {"quantity_g", qty}, {"confirmed_price_paise_per_kg", price}};
        body.update(extra);
        return c.post("/pools/" + std::to_string(pool) + "/sales", body);
    }
    static std::string P(i64 pool) { return "/pools/" + std::to_string(pool); }
};

// ---- authentication -----------------------------------------------------------------------------------------------

TEST_F(E4Test, LoginSessionCookieAndForcedPasswordChange) {
    const std::string username = unique_name("newbie");
    ASSERT_GT(server->create_account("New Person", username, "FARMER", kTemp), 0);

    Client c = anon();
    const Response wrong = c.login(username, "not-the-password");
    const Response unknown = c.login(unique_name("nobody"), "not-the-password");
    EXPECT_EQ(wrong.status, 401);
    EXPECT_EQ(unknown.status, 401);
    EXPECT_EQ(wrong.body, unknown.body) << "one generic answer: nobody can tell which usernames exist";
    EXPECT_TRUE(wrong.set_cookies.empty());

    const Response ok = c.login(username, kTemp);
    ASSERT_EQ(ok.status, 200);
    EXPECT_EQ(ok.j()["role"], "farmer");
    EXPECT_TRUE(ok.j()["must_change_password"].get<bool>());
    ASSERT_FALSE(ok.set_cookies.empty());
    EXPECT_NE(ok.set_cookies[0].find("HttpOnly"), std::string::npos);
    EXPECT_NE(ok.set_cookies[0].find("SameSite=Strict"), std::string::npos);
    EXPECT_NE(ok.set_cookies[0].find("Max-Age=28800"), std::string::npos) << "8 hours";
    EXPECT_EQ(c.cookie.size(), std::string("agri_session=").size() + 64) << "256-bit random token as hex";
    EXPECT_EQ(ok.body.find("hash"), std::string::npos);

    // until the temporary password is changed everything else is refused
    EXPECT_EQ(c.get("/pools").status, 403);
    EXPECT_EQ(c.get("/pools").j()["error"], "PASSWORD_CHANGE_REQUIRED");
    EXPECT_EQ(c.post("/auth/change-password", {{"current_password", "wrong-current"}, {"new_password", kPass}}).status, 403);
    EXPECT_EQ(c.post("/auth/change-password", {{"current_password", kTemp}, {"new_password", "short"}}).status, 400);
    EXPECT_EQ(c.post("/auth/change-password", {{"current_password", kTemp}, {"new_password", kTemp}}).status, 400);
    EXPECT_EQ(c.post("/auth/change-password", {{"current_password", kTemp}, {"new_password", kPass}}).status, 200);
    EXPECT_EQ(c.get("/pools").status, 200);

    EXPECT_EQ(anon().login(username, kTemp).status, 401) << "the temporary password stops working";
    EXPECT_EQ(anon().login(username, kPass).status, 200);
}

TEST_F(E4Test, LogoutEndsTheSession_ExpiredSessionsAreRefused) {
    build_world();
    Client c = session_of(farmer[0]);
    const std::string old_cookie = c.cookie;
    ASSERT_EQ(c.get("/pools").status, 200);
    const Response out = c.post_empty("/auth/logout");
    EXPECT_EQ(out.status, 200);
    ASSERT_FALSE(out.set_cookies.empty());
    EXPECT_NE(out.set_cookies[0].find("Max-Age=0"), std::string::npos);
    Client replay = anon();
    replay.cookie = old_cookie;
    EXPECT_EQ(replay.get("/pools").status, 401) << "a logged-out token is dead";

    Client e = session_of(farmer[1]);
    ASSERT_EQ(e.get("/pools").status, 200);
    tamper("UPDATE session_token SET created_at = now() - interval '2 hours', expires_at = now() - interval '1 second' WHERE user_id = $1", farmer[1].id);
    EXPECT_EQ(e.get("/pools").status, 401) << "an expired session is refused";

    Client forged = anon();
    forged.cookie = "agri_session=" + std::string(64, 'a');
    EXPECT_EQ(forged.get("/pools").status, 401);
    forged.cookie = "agri_session=' OR 1=1 --";
    EXPECT_EQ(forged.get("/pools").status, 401);
}

TEST_F(E4Test, LoginIsThrottledAfterRepeatedFailures) {
    const std::string who = unique_name("victim");
    Client c = anon();
    for (int i = 0; i < 5; ++i) EXPECT_EQ(c.login(who, "wrong-pass-" + std::to_string(i)).status, 401);
    const Response blocked = c.login(who, "wrong-again");
    EXPECT_EQ(blocked.status, 429);
    EXPECT_TRUE(blocked.headers.count("retry-after"));
    EXPECT_EQ(c.login(unique_name("someone-else"), "x").status, 401) << "other names are not affected";
}

TEST_F(E4Test, JsonEndpointsRequireJsonAndRefuseBadBodies) {
    build_world();
    const i64 pool = make_worked_example_pool();
    EXPECT_EQ(buyer_c[0].post_raw(P(pool) + "/sales", "request_id=1", "application/x-www-form-urlencoded").status, 415) << "cross-site form posts are refused";
    EXPECT_EQ(buyer_c[0].post_raw(P(pool) + "/sales", "{not json", "application/json").status, 400);
    EXPECT_EQ(buyer_c[0].post_raw(P(pool) + "/sales", "[1,2]", "application/json").status, 400);
    EXPECT_EQ(buyer_c[0].post_raw(P(pool) + "/sales", std::string(70000, ' ') + "{}", "application/json").status, 413);
}

// ---- role matrix (T-FR-08): every endpoint x every role ------------------------------------------------------------

TEST_F(E4Test, RoleMatrixOverEveryEndpoint) {
    build_world();
    const i64 pool = make_worked_example_pool();
    const std::string photo_url = upload(mgr, pool, kPng + "matrix").j()["url"];
    const Person* people[3] = {&manager, &farmer[0], &buyer[0]};
    const int bits[3] = {kM, kF, kB};

    struct Ep { std::string name; int roles; std::function<Response(Client&, int)> run; };
    int counter = 0;
    const std::vector<Ep> eps = {
        {"GET /users", kM, [&](Client& c, int) { return c.get("/users"); }},
        {"POST /users", kM, [&](Client& c, int) { return c.post("/users", {{"name", "X"}, {"username", unique_name("m")}, {"type", "BUYER"}, {"temporary_password", kTemp}}); }},
        {"GET /reference/commodities", kAny, [&](Client& c, int) { return c.get("/reference/commodities"); }},
        {"GET /reference/prices", kAny, [&](Client& c, int) { return c.get("/reference/prices"); }},
        {"GET /pools", kAny, [&](Client& c, int) { return c.get("/pools"); }},
        {"POST /pools", kM, [&](Client& c, int) { return c.post("/pools", {{"commodity_id", paddy_id()}, {"variety", "V"}, {"range_no", 2}, {"storage_location", "S"}, {"min_order_g", 1000}, {"price_paise_per_kg", 2000}}); }},
        {"GET /pools/{id}", kAny, [&](Client& c, int) { return c.get(P(pool)); }},
        {"PATCH /pools/{id}", kM, [&](Client& c, int) { return c.patch(P(pool), {{"price_paise_per_kg", 2000 + ++counter}}); }},
        {"GET /pools/{id}/photos", kAny, [&](Client& c, int) { return c.get(P(pool) + "/photos"); }},
        {"POST /pools/{id}/photos", kM, [&](Client& c, int) { return upload(c, pool, kPng + std::to_string(++counter)); }},
        {"POST /pools/{id}/contributions", kM, [&](Client& c, int) { return c.post(P(pool) + "/contributions", {{"seller_id", farmer[0].id}, {"quantity_g", 1000}}); }},
        {"POST /pools/{id}/sales", kB | kM, [&](Client& c, int role) {
            json extra = role == kM ? json{{"buyer_id", buyer[0].id}} : json::object();
            return sale(c, pool, 100000, 2500, extra); }},
        {"GET /pools/{id}/ledgers/inventory", kM, [&](Client& c, int) { return c.get(P(pool) + "/ledgers/inventory"); }},
        {"GET /pools/{id}/ledgers/ownership", kM, [&](Client& c, int) { return c.get(P(pool) + "/ledgers/ownership"); }},
        {"GET /pools/{id}/ledgers/financial", kM, [&](Client& c, int) { return c.get(P(pool) + "/ledgers/financial"); }},
        {"GET /pools/{id}/reconciliation", kAny, [&](Client& c, int) { return c.get(P(pool) + "/reconciliation"); }},
        {"POST /pools/{id}/integrity-check", kM, [&](Client& c, int) { return c.post_empty(P(pool) + "/integrity-check"); }},
        {"GET /me/pools", kF, [&](Client& c, int) { return c.get("/me/pools"); }},
        {"GET /me/purchases", kB, [&](Client& c, int) { return c.get("/me/purchases"); }},
        {"GET /farmers/{id}/statement", kM | kF, [&](Client& c, int) { return c.get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)); }},
        {"GET /photos/{name}", kAny, [&](Client& c, int) { return c.get(photo_url); }},
    };

    auto state = [&] {
        pqxx::connection admin(super_conninfo());
        pqxx::nontransaction n(admin);
        auto s = snapshot(pool);
        s["users"] = n.exec1("SELECT count(*) FROM app_user")[0].as<i64>();
        s["pools"] = n.exec1("SELECT count(*) FROM pool")[0].as<i64>();
        s["price"] = n.exec_params1("SELECT price_paise_per_kg FROM pool WHERE pool_id = $1", pool)[0].as<i64>();
        return s;
    };

    for (const Ep& ep : eps) {
        {   // not logged in: 401 and nothing changes
            Client a = anon();
            const auto before = state();
            const Response r = ep.run(a, 0);
            EXPECT_EQ(r.status, 401) << ep.name << " anonymous";
            EXPECT_EQ(state(), before) << ep.name << " anonymous changed state";
        }
        for (int i = 0; i < 3; ++i) {
            Client c = session_of(*people[i]);
            if (ep.roles & bits[i]) {
                const Response r = ep.run(c, bits[i]);
                EXPECT_NE(r.status, 401) << ep.name << " as role " << bits[i];
                EXPECT_NE(r.status, 403) << ep.name << " as role " << bits[i];
                EXPECT_LT(r.status, 500) << ep.name << " as role " << bits[i] << " " << r.body;
            } else {
                const auto before = state();
                const Response r = ep.run(c, bits[i]);
                EXPECT_EQ(r.status, 403) << ep.name << " as role " << bits[i] << " " << r.body;
                EXPECT_EQ(r.j().value("error", ""), "FORBIDDEN") << ep.name;
                EXPECT_EQ(state(), before) << ep.name << " refused for role " << bits[i] << " but state changed";
            }
        }
    }

    // a Farmer may read only their own statement
    EXPECT_EQ(farmer_c[1].get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)).status, 403);
    EXPECT_EQ(farmer_c[0].get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)).status, 200);
    // unknown routes and wrong methods do not leak anything
    EXPECT_EQ(anon().get("/nope").status, 404);
    EXPECT_NE(buyer_c[0].post("/pools", json::object()).status, 201);
}

// ---- who sees what -----------------------------------------------------------------------------------------------

TEST_F(E4Test, BuyersNeverSeeFarmerNamesSharesOrPayouts) {
    build_world();
    const i64 pool = make_worked_example_pool();
    const Response bought = sale(buyer_c[0], pool, 500000);
    ASSERT_EQ(bought.status, 200) << bought.body;

    const std::set<std::string> forbidden = {"farmers", "payouts", "my_share", "share_permille", "seller_id", "seller_name", "remaining_g", "deposited_g", "sold_g", "alloc_g"};
    for (const Response& r : {buyer_c[0].get("/pools"), buyer_c[0].get(P(pool)), buyer_c[0].get(P(pool) + "/photos"), bought,
                              buyer_c[0].get("/me/purchases"), buyer_c[0].get(P(pool) + "/reconciliation")}) {
        EXPECT_EQ(r.status, 200);
        EXPECT_FALSE(has_key(r.j(), forbidden)) << r.body;
        for (const char* name : {"Zorbax", "Quillon", "Wexley"}) EXPECT_EQ(r.body.find(name), std::string::npos) << name << " leaked: " << r.body;
    }
    EXPECT_EQ(buyer_c[0].get(P(pool)).j()["farmer_count"], 3) << "only the number of farmers";

    // a Farmer sees only their own share, not other farmers
    const json detail = farmer_c[0].get(P(pool)).j();
    EXPECT_TRUE(detail.contains("my_share"));
    EXPECT_FALSE(detail.contains("farmers"));
    EXPECT_EQ(farmer_c[0].get(P(pool)).body.find("Quillon"), std::string::npos);
    EXPECT_EQ(farmer_c[0].get(P(pool)).body.find("Wexley"), std::string::npos);

    // draft and closed pools are invisible to buyers
    const i64 draft = make_draft_pool();
    EXPECT_EQ(buyer_c[0].get(P(draft)).status, 404);
    EXPECT_EQ(buyer_c[0].get(P(draft) + "/photos").status, 404);
    EXPECT_EQ(buyer_c[0].get(P(draft) + "/reconciliation").status, 404);
    for (const json& p : buyer_c[0].get("/pools").j()) EXPECT_NE(p["pool_id"].get<i64>(), draft);
    ASSERT_EQ(mgr.patch(P(pool), {{"status", "closed"}}).status, 200);
    EXPECT_EQ(buyer_c[0].get(P(pool)).status, 404);
    for (const json& p : buyer_c[0].get("/pools").j()) EXPECT_NE(p["pool_id"].get<i64>(), pool);
    EXPECT_EQ(farmer_c[0].get(P(pool)).status, 200) << "a farmer with a share still sees their closed pool";
    EXPECT_EQ(farmer_c[0].get(P(draft)).status, 404);
}

TEST_F(E4Test, UserListNeverContainsPasswordHashes) {
    build_world();
    const Response r = mgr.get("/users");
    ASSERT_EQ(r.status, 200);
    EXPECT_EQ(r.body.find("argon2"), std::string::npos);
    EXPECT_FALSE(has_key(r.j(), {"password_hash", "password"}));
    bool waiting = false;
    const std::string waiting_user = unique_name("waiting");
    server->create_account("Waiting Person", waiting_user, "BUYER", kTemp);
    for (const json& u : mgr.get("/users").j()) if (u["username"] == waiting_user) waiting = u["status"] == "waiting_first_login";
    EXPECT_TRUE(waiting);
}

// ---- sales over HTTP: every reason code, validation, duplicates --------------------------------------------------

TEST_F(E4Test, SaleReasonCodesAndPlainBodies) {
    build_world();
    const i64 pool = make_open_pool(2500, 100000);
    deposit(pool, farmer[0], 500000);

    // not open
    const i64 draft = make_draft_pool();
    deposit(draft, farmer[0], 500000);
    json r = sale(buyer_c[0], draft, 100000).j();
    EXPECT_EQ(r["reason"], "POOL_NOT_OPEN");
    EXPECT_EQ(sale(buyer_c[0], draft, 100000).status, 409);

    // below minimum
    Response resp = sale(buyer_c[0], pool, 50000);
    EXPECT_EQ(resp.status, 409);
    EXPECT_EQ(resp.j()["status"], "REJECTED");
    EXPECT_EQ(resp.j()["reason"], "BELOW_MIN_ORDER");
    EXPECT_EQ(resp.j()["min_order_g"], 100000);

    // price changed
    ASSERT_EQ(mgr.patch(P(pool), {{"price_paise_per_kg", 2400}}).status, 200);
    resp = sale(buyer_c[0], pool, 200000, 2500);
    EXPECT_EQ(resp.j()["reason"], "PRICE_CHANGED");
    EXPECT_EQ(resp.j()["current_price_paise_per_kg"], 2400);

    // insufficient stock
    resp = sale(buyer_c[0], pool, 600000, 2400);
    EXPECT_EQ(resp.status, 409);
    EXPECT_EQ(resp.j()["reason"], "INSUFFICIENT_STOCK");
    EXPECT_EQ(resp.j()["stock_g"], 500000);

    // accepted: a buyer's receipt has no payouts
    resp = sale(buyer_c[0], pool, 200000, 2400);
    ASSERT_EQ(resp.status, 200) << resp.body;
    const json ok = resp.j();
    EXPECT_EQ(ok["status"], "ACCEPTED");
    EXPECT_FALSE(ok["duplicate"].get<bool>());
    EXPECT_EQ(ok["gross_paise"], 480000);
    EXPECT_EQ(ok["stock_after_g"], 300000);
    EXPECT_EQ(ok["unit_price_paise_per_kg"], 2400);
    EXPECT_EQ(ok["storage_location"], "Test godown");
    EXPECT_EQ(ok["receipt_no"].get<std::string>().substr(0, 2), "R-");
    EXPECT_FALSE(ok.contains("payouts"));

    // the whole remainder is allowed below the minimum, then the pool is out of stock (and stays open)
    ASSERT_EQ(mgr.patch(P(pool), {{"price_paise_per_kg", 2400}}).status, 409) << "no change is refused";
    resp = sale(buyer_c[1], pool, 300000, 2400);
    ASSERT_EQ(resp.status, 200);
    resp = sale(buyer_c[1], pool, 100000, 2400);
    EXPECT_EQ(resp.status, 409);
    EXPECT_EQ(resp.j()["reason"], "OUT_OF_STOCK");
    EXPECT_EQ(mgr.get(P(pool)).j()["status"], "open");

    // a closed pool refuses sales
    deposit(pool, farmer[1], 500000);
    ASSERT_EQ(mgr.patch(P(pool), {{"status", "closed"}}).status, 200);
    EXPECT_EQ(sale(buyer_c[0], pool, 100000, 2400).j()["reason"], "POOL_NOT_OPEN");
    EXPECT_EQ(mgr.post(P(pool) + "/contributions", {{"seller_id", farmer[1].id}, {"quantity_g", 1000}}).status, 409);
    ASSERT_EQ(mgr.patch(P(pool), {{"status", "open"}}).status, 200) << "the Manager may reopen";
    EXPECT_EQ(sale(buyer_c[0], pool, 100000, 2400).status, 200);
}

TEST_F(E4Test, BusyWhenThePoolStaysLocked_ThenRecovers) {
    build_world();
    const i64 pool = make_worked_example_pool();
    pqxx::connection holder(super_conninfo());
    pqxx::work hold(holder);
    hold.exec_params("SELECT 1 FROM pool WHERE pool_id = $1 FOR UPDATE", pool);
    const Response r = sale(buyer_c[0], pool, 100000);
    EXPECT_EQ(r.status, 503);
    EXPECT_EQ(r.j()["reason"], "BUSY");
    EXPECT_TRUE(r.headers.count("retry-after"));
    hold.abort();
    EXPECT_EQ(sale(buyer_c[0], pool, 100000).status, 200);
}

TEST_F(E4Test, SaleInputValidation) {
    build_world();
    const i64 pool = make_worked_example_pool();
    Client& b = buyer_c[0];
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", new_uuid()}, {"quantity_g", 100000.5}, {"confirmed_price_paise_per_kg", 2500}}).status, 400) << "no decimals for grams";
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", new_uuid()}, {"quantity_g", "100000"}, {"confirmed_price_paise_per_kg", 2500}}).status, 400) << "no strings for numbers";
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", new_uuid()}, {"quantity_g", 0}, {"confirmed_price_paise_per_kg", 2500}}).status, 400);
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", new_uuid()}, {"quantity_g", -5}, {"confirmed_price_paise_per_kg", 2500}}).status, 400);
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"quantity_g", 100000}, {"confirmed_price_paise_per_kg", 2500}}).status, 400) << "request_id is required";
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", "not-a-uuid-not-a-uuid-not-a-uuid-12345"}, {"quantity_g", 100000}, {"confirmed_price_paise_per_kg", 2500}}).status, 400);
    EXPECT_EQ(b.post(P(pool) + "/sales", {{"request_id", new_uuid()}, {"quantity_g", 100000}}).status, 400) << "the confirmed price is required";
    EXPECT_EQ(sale(b, 999999999, 100000).status, 404);
    EXPECT_EQ(sale(b, pool, 100000, 2500, {{"buyer_id", buyer[1].id}}).status, 403) << "a buyer cannot buy as someone else";
    EXPECT_EQ(sale(mgr, pool, 100000).status, 400) << "the Manager must say which buyer";
    EXPECT_EQ(sale(mgr, pool, 100000, 2500, {{"buyer_id", 999999999}}).status, 404);
    EXPECT_EQ(sale(mgr, pool, 100000, 2500, {{"buyer_id", farmer[0].id}}).status, 404) << "a farmer is not a buyer";
}

TEST_F(E4Test, ManagerSaleForABuyerShowsPayoutsAndStaysPrivateToThatBuyer) {
    build_world();
    const i64 pool = make_worked_example_pool();
    const std::string rid = new_uuid();
    const Response r = sale(mgr, pool, 500000, 2500, {{"buyer_id", buyer[1].id}}, rid);
    ASSERT_EQ(r.status, 200) << r.body;
    const json j = r.j();
    ASSERT_TRUE(j.contains("payouts"));
    ASSERT_EQ(j["payouts"].size(), 3u);
    EXPECT_EQ(j["gross_paise"], 1250000);
    EXPECT_EQ(j["payouts"][0]["alloc_g"], 150000);
    EXPECT_EQ(j["payouts"][0]["paise"], 375000);
    EXPECT_EQ(j["payouts"][1]["paise"], 500000);
    EXPECT_EQ(j["payouts"][2]["paise"], 375000);
    EXPECT_EQ(buyer_c[1].get("/me/purchases").j().size(), 1u);
    EXPECT_EQ(buyer_c[0].get("/me/purchases").j().size(), 0u) << "each buyer sees only their own receipts";
    pqxx::connection admin(super_conninfo());
    pqxx::nontransaction n(admin);
    const pqxx::row row = n.exec_params1("SELECT e.actor_user_id, s.buyer_id FROM sale s JOIN event e USING (event_id) WHERE s.request_id = $1::uuid", rid);
    EXPECT_EQ(row[0].as<i64>(), manager.id) << "who placed the sale is recorded";
    EXPECT_EQ(row[1].as<i64>(), buyer[1].id);
}

TEST_F(E4Test, DuplicateRequestIdOverHttp) {
    build_world();
    const i64 pool = make_worked_example_pool();
    const std::string rid = new_uuid();
    const Response first = sale(buyer_c[0], pool, 500000, 2500, json::object(), rid);
    ASSERT_EQ(first.status, 200);
    const Response second = sale(buyer_c[0], pool, 500000, 2500, json::object(), rid);   // the double tap
    ASSERT_EQ(second.status, 200);
    EXPECT_TRUE(second.j()["duplicate"].get<bool>());
    EXPECT_EQ(second.j()["sale_id"], first.j()["sale_id"]);
    EXPECT_EQ(mgr.get(P(pool)).j()["stock_g"], 1500000) << "stock was cut once";
    EXPECT_EQ(buyer_c[0].get("/me/purchases").j().size(), 1u);
    EXPECT_EQ(sale(buyer_c[1], pool, 500000, 2500, json::object(), rid).status, 409) << "another buyer cannot reuse the id";
}

// ---- pool management, photos and accounts ------------------------------------------------------------------------

TEST_F(E4Test, PoolCreationAndStatusValidation) {
    build_world();
    const json base = {{"commodity_id", paddy_id()}, {"variety", "Sona Masuri"}, {"range_no", 1}, {"storage_location", "Godown"}, {"min_order_g", 100000}, {"price_paise_per_kg", 2500}};
    auto with = [&](const std::string& key, const json& value) { json b = base; b[key] = value; return mgr.post("/pools", b); };
    EXPECT_EQ(mgr.post("/pools", base).status, 201);
    EXPECT_EQ(with("price_paise_per_kg", 2500.5).status, 400) << "whole rupees per quintal only: a decimal is refused";
    EXPECT_EQ(with("price_paise_per_kg", "2500").status, 400);
    EXPECT_EQ(with("price_paise_per_kg", 0).status, 400);
    EXPECT_EQ(with("range_no", 4).status, 400);
    EXPECT_EQ(with("range_no", 0).status, 400);
    EXPECT_EQ(with("min_order_g", 0).status, 400);
    EXPECT_EQ(with("variety", "").status, 400);
    EXPECT_EQ(with("commodity_id", 999999999).status, 404);
    json missing = base;
    missing.erase("storage_location");
    EXPECT_EQ(mgr.post("/pools", missing).status, 400);

    const i64 pool = make_draft_pool();
    EXPECT_EQ(mgr.patch(P(pool), {{"status", "open"}}).status, 409) << "a pool cannot open without a photo";
    EXPECT_EQ(mgr.patch(P(pool), {{"status", "open"}}).j()["error"], "PHOTO_REQUIRED");
    EXPECT_EQ(mgr.patch(P(pool), json::object()).status, 400);
    EXPECT_EQ(mgr.patch(P(pool), {{"status", "draft"}}).status, 400);
    EXPECT_EQ(mgr.patch(P(pool), {{"price_paise_per_kg", 2400.5}}).status, 400);
    EXPECT_EQ(mgr.patch(P(999999999), {{"status", "open"}}).status, 404);
    EXPECT_EQ(mgr.post(P(pool) + "/contributions", {{"seller_id", farmer[0].id}, {"quantity_g", 0}}).status, 400);
    EXPECT_EQ(mgr.post(P(pool) + "/contributions", {{"seller_id", buyer[0].id}, {"quantity_g", 1000}}).status, 404) << "a buyer is not a farmer";
}

TEST_F(E4Test, AccountCreationRules) {
    build_world();
    const std::string name = unique_name("dupe");
    EXPECT_EQ(mgr.post("/users", {{"name", "A"}, {"username", name}, {"type", "FARMER"}, {"temporary_password", kTemp}}).status, 201);
    EXPECT_EQ(mgr.post("/users", {{"name", "B"}, {"username", name}, {"type", "BUYER"}, {"temporary_password", kTemp}}).status, 409);
    EXPECT_EQ(mgr.post("/users", {{"name", "B"}, {"username", unique_name("m")}, {"type", "MANAGER"}, {"temporary_password", kTemp}}).status, 400) << "no Manager can be created through the API";
    EXPECT_EQ(mgr.post("/users", {{"name", "B"}, {"username", unique_name("m")}, {"type", "BUYER"}, {"temporary_password", "short"}}).status, 400);
    EXPECT_EQ(mgr.post("/users", {{"name", "B"}, {"username", "Has Spaces"}, {"type", "BUYER"}, {"temporary_password", kTemp}}).status, 400);
    EXPECT_EQ(mgr.post("/users", {{"name", "B"}, {"username", unique_name("m")}, {"type", "BUYER"}}).status, 400);
}

TEST_F(E4Test, PhotoUploadChecksContentSizeAndNeverUsesTheClientsName) {
    build_world();
    const i64 pool = make_draft_pool();
    // a real PNG signature, a hostile file name and a wrong declared type: only the content matters
    const Response ok = upload(mgr, pool, kPng + "one", "../../etc/evil.php", "text/x-php");
    ASSERT_EQ(ok.status, 201) << ok.body;
    const std::string url = ok.j()["url"];
    ASSERT_EQ(url.size(), std::string("/photos/").size() + 64 + 4);
    EXPECT_EQ(url.substr(url.size() - 4), ".png");
    EXPECT_EQ(url.find("evil"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(photo_dir) / url.substr(8)));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(photo_dir) / "evil.php"));

    // served to any logged-in user with the right type and no sniffing
    const Response got = buyer_c[0].get(url);
    EXPECT_EQ(got.status, 200);
    EXPECT_EQ(got.body, kPng + "one");
    EXPECT_EQ(got.headers.at("content-type"), "image/png");
    EXPECT_EQ(got.headers.at("x-content-type-options"), "nosniff");
    EXPECT_EQ(anon().get(url).status, 401);

    // content checks
    EXPECT_EQ(upload(mgr, pool, "<?php echo 1; ?>", "x.jpg", "image/jpeg").status, 415) << "a script named .jpg is refused";
    EXPECT_EQ(upload(mgr, pool, "GIF89a....", "x.gif", "image/gif").status, 415);
    EXPECT_EQ(upload(mgr, pool, kPng + std::string(150 * 1024, 'z')).status, 413) << "over the 5 MB limit (100 KB in this test server)";
    EXPECT_EQ(upload(mgr, pool, "").status, 400);
    EXPECT_EQ(mgr.post_raw(P(pool) + "/photos", "{}", "application/json").status, 415);
    EXPECT_EQ(mgr.post_raw(P(pool) + "/photos", testhttp::multipart_body("b1", "other", "a.png", "image/png", kPng), "multipart/form-data; boundary=b1").status, 400);
    EXPECT_EQ(upload(mgr, 999999999, kPng + "two").status, 404);
    EXPECT_EQ(upload(farmer_c[0], pool, kPng + "three").status, 403);
    EXPECT_EQ(upload(buyer_c[0], pool, kPng + "four").status, 403);

    // path traversal and bad names
    for (const char* bad : {"/photos/..%2F..%2Fetc%2Fpasswd", "/photos/..", "/photos/zzzz.png", "/photos/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.png"})
        EXPECT_EQ(buyer_c[0].get(bad).status, 404) << bad;

    // buyers see the three newest photos; the Manager sees all of them
    const i64 open_pool = make_open_pool();
    for (int i = 0; i < 3; ++i) ASSERT_EQ(upload(mgr, open_pool, kPng + "extra" + std::to_string(i)).status, 201);
    ASSERT_EQ(mgr.get(P(open_pool) + "/photos").j().size(), 4u);
    const json seen = buyer_c[0].get(P(open_pool) + "/photos").j();
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(buyer_c[0].get(P(open_pool)).j()["photos"].size(), 3u);
    EXPECT_GT(seen[0]["photo_id"].get<i64>(), seen[2]["photo_id"].get<i64>()) << "newest first";
}

// ---- reads: ledgers, statements, purchases, reference data -----------------------------------------------------

TEST_F(E4Test, ViewsMatchTheWorkedExample) {
    build_world();
    const i64 pool = make_worked_example_pool();
    ASSERT_EQ(sale(buyer_c[0], pool, 500000).status, 200);
    ASSERT_EQ(mgr.patch(P(pool), {{"price_paise_per_kg", 2400}}).status, 200);

    // Manager's pool detail: shares in permille (one decimal of a percent), quality limits as text, reference prices
    const json d = mgr.get(P(pool)).j();
    EXPECT_EQ(d["stock_g"], 1500000);
    EXPECT_EQ(d["deposited_g"], 2000000);
    EXPECT_EQ(d["sold_g"], 500000);
    EXPECT_TRUE(d["records_ok"].get<bool>());
    ASSERT_EQ(d["farmers"].size(), 3u);
    EXPECT_EQ(d["farmers"][0]["share_permille"], 300);
    EXPECT_EQ(d["farmers"][1]["share_permille"], 400);
    EXPECT_EQ(d["farmers"][2]["share_permille"], 300);
    EXPECT_EQ(d["farmers"][1]["remaining_g"], 600000);
    ASSERT_EQ(d["limits"].size(), 5u);
    EXPECT_TRUE(d["limits"][0]["max"].is_string()) << "limits are decimal text, never floating point";
    EXPECT_EQ(d["reference"]["msp"]["price_paise_per_quintal"], 244100);
    EXPECT_TRUE(d["reference"]["mandi_modal"].is_null());
    EXPECT_FALSE(d["limits_verified"].get<bool>());
    EXPECT_EQ(d["first_deposit_at"].get<std::string>().size(), 10u);

    // Farmer A: own share only
    const json mine = farmer_c[0].get(P(pool)).j()["my_share"];
    EXPECT_EQ(mine["remaining_g"], 450000);
    EXPECT_EQ(mine["share_permille"], 300);
    EXPECT_EQ(mine["deposited_g"], 600000);
    EXPECT_EQ(mine["paid_paise"], 375000);
    const json pools = farmer_c[0].get("/me/pools").j();
    ASSERT_EQ(pools.size(), 1u);
    EXPECT_EQ(pools[0]["my_remaining_g"], 450000);
    EXPECT_EQ(pools[0]["paid_to_me_paise"], 375000);
    EXPECT_EQ(pools[0]["activity"][0]["kind"], "sale");
    EXPECT_EQ(pools[0]["activity"][0]["my_delta_g"], -150000);
    EXPECT_EQ(pools[0]["activity"][0]["pool_sale_g"], 500000);
    EXPECT_EQ(pools[0]["activity"][0]["paise"], 375000);
    EXPECT_EQ(pools[0]["activity"][1]["kind"], "deposit");

    // statement: Manager for any farmer, the farmer for themselves
    for (const json& st : {mgr.get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)).j(),
                           farmer_c[0].get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)).j()}) {
        EXPECT_EQ(st["summary"]["deposited_g"], 600000);
        EXPECT_EQ(st["summary"]["sold_g"], 150000);
        EXPECT_EQ(st["summary"]["paid_paise"], 375000);
        EXPECT_EQ(st["summary"]["left_g"], 450000);
        EXPECT_EQ(st["summary"]["share_permille"], 300);
        EXPECT_TRUE(st["records_ok"].get<bool>());
        ASSERT_EQ(st["rows"].size(), 2u);
        EXPECT_EQ(st["rows"][0]["kind"], "sale");
        EXPECT_EQ(st["rows"][0]["delta_g"], -150000);
        EXPECT_EQ(st["rows"][0]["paise"], 375000);
        EXPECT_EQ(st["rows"][0]["share_permille"], 300) << "this farmer's part of that sale";
        EXPECT_EQ(st["rows"][1]["kind"], "deposit");
        EXPECT_TRUE(st["rows"][1]["paise"].is_null());
    }
    EXPECT_EQ(mgr.get("/farmers/" + std::to_string(farmer[0].id) + "/statement").status, 400) << "pool is required";
    EXPECT_EQ(mgr.get("/farmers/" + std::to_string(buyer[0].id) + "/statement?pool=" + std::to_string(pool)).status, 404);

    // buyer's receipt list
    const json rec = buyer_c[0].get("/me/purchases").j();
    ASSERT_EQ(rec.size(), 1u);
    EXPECT_EQ(rec[0]["gross_paise"], 1250000);
    EXPECT_EQ(rec[0]["quantity_g"], 500000);
    EXPECT_EQ(rec[0]["storage_location"], "Test godown");

    // ledgers (Manager): newest first, paged; price changes ride along in the stock feed as different rows
    const json inv = mgr.get(P(pool) + "/ledgers/inventory").j();
    EXPECT_EQ(inv["rows"][0]["movement_type"], "OUT");
    EXPECT_EQ(inv["rows"][0]["delta_g"], -500000);
    EXPECT_EQ(inv["rows"][0]["resulting_stock_g"], 1500000);
    bool saw_price_change = false;
    for (const json& c : inv["changes"]) if (c["field"] == "PRICE" && c["old_value"] == "2500" && c["new_value"] == "2400" && c["kind"] == "change") saw_price_change = true;
    EXPECT_TRUE(saw_price_change);
    const json own1 = mgr.get(P(pool) + "/ledgers/ownership?limit=2").j();
    ASSERT_EQ(own1["rows"].size(), 2u);
    ASSERT_TRUE(own1["next_before_id"].is_number());
    const json own2 = mgr.get(P(pool) + "/ledgers/ownership?limit=200&before=" + std::to_string(own1["next_before_id"].get<i64>())).j();
    EXPECT_EQ(own2["rows"].size(), 4u) << "6 ownership rows in all: 3 deposits and 3 allocations";
    const json fin = mgr.get(P(pool) + "/ledgers/financial").j();
    bool gross = false;
    for (const json& r : fin["rows"]) if (r["entry_type"] == "SALE_GROSS" && r["amount_paise"] == 1250000 && r["seller_id"].is_null()) gross = true;
    EXPECT_TRUE(gross);
    EXPECT_EQ(mgr.get(P(pool) + "/ledgers/secrets").status, 400);
    EXPECT_EQ(mgr.get(P(pool) + "/ledgers/inventory?limit=0").status, 400);
    EXPECT_EQ(mgr.get(P(pool) + "/ledgers/inventory?before=abc").status, 400);
    EXPECT_EQ(mgr.get("/pools/999999999/ledgers/inventory").status, 404);

    // reference data
    bool paddy_limits = false;
    for (const json& c : mgr.get("/reference/commodities").j())
        if (c["name"] == "Paddy") for (const json& p : c["parameters"]) if (p["name"] == "Moisture" && p["range1_max"] == "17.00") paddy_limits = true;
    EXPECT_TRUE(paddy_limits);
    bool msp = false;
    for (const json& p : mgr.get("/reference/prices").j()) if (p["kind"] == "MSP" && p["price_paise_per_quintal"] == 244100) msp = true;
    EXPECT_TRUE(msp);

    // buyer list shows the open pool with the new price
    bool listed = false;
    for (const json& p : buyer_c[1].get("/pools").j()) if (p["pool_id"] == pool) listed = p["price_paise_per_kg"] == 2400 && p["stock_g"] == 1500000;
    EXPECT_TRUE(listed);
    // Manager's list carries the records badge
    for (const json& p : mgr.get("/pools").j()) {
        if (p["pool_id"] == pool) { EXPECT_EQ(p["records"], "ok"); }
    }
}

TEST_F(E4Test, OutOfStockPoolStaysVisibleToBuyersForADay) {
    build_world();
    const i64 pool = make_open_pool();
    deposit(pool, farmer[0], 200000);
    auto listed = [&] { for (const json& p : buyer_c[0].get("/pools").j()) if (p["pool_id"] == pool) return true; return false; };
    ASSERT_EQ(sale(buyer_c[0], pool, 200000).status, 200);
    EXPECT_TRUE(listed()) << "an open pool with stock 0 stays on the list, greyed, for 24 hours";
    EXPECT_EQ(buyer_c[0].get(P(pool)).j()["stock_g"], 0);
    EXPECT_EQ(buyer_c[0].get(P(pool)).j()["status"], "open");
    tamper("UPDATE sale SET created_at = now() - interval '25 hours' WHERE pool_id = $1", pool);
    EXPECT_FALSE(listed()) << "after a day it drops off until restocked";
    deposit(pool, farmer[1], 300000);
    EXPECT_TRUE(listed());
}

// ---- integrity check ---------------------------------------------------------------------------------------------

TEST_F(E4Test, IntegrityCheckNamesTheTamperedEntry) {
    build_world();
    const i64 pool = make_worked_example_pool();
    ASSERT_EQ(sale(buyer_c[0], pool, 500000).status, 200);
    json ic = mgr.post_empty(P(pool) + "/integrity-check").j();
    EXPECT_TRUE(ic["ok"].get<bool>());
    EXPECT_EQ(ic["checked_entries"], 6);
    EXPECT_FALSE(ic.contains("first_broken"));

    tamper("UPDATE financial_ledger SET amount_paise = amount_paise + 1 WHERE pool_id = $1 AND entry_type = 'SELLER_PAYOUT' AND seller_id = $2", pool, farmer[1].id);
    ic = mgr.post_empty(P(pool) + "/integrity-check").j();
    EXPECT_FALSE(ic["ok"].get<bool>());
    EXPECT_EQ(ic["first_broken"]["sequence_no"], 6);
    EXPECT_EQ(ic["first_broken"]["event_type"], "SALE");
    EXPECT_EQ(ic["first_broken"]["reason"], "HASH_MISMATCH");
    EXPECT_EQ(ic["first_broken"].size(), 3u) << "no raw hashes are ever returned";

    EXPECT_TRUE(mgr.get(P(pool) + "/reconciliation").j()["ok"].get<bool>()) << "the totals still add up; only the hash chain notices";
    EXPECT_FALSE(mgr.get(P(pool) + "/reconciliation").j().contains("hash"));
    const json plain = buyer_c[0].get(P(pool) + "/reconciliation").j();
    EXPECT_EQ(plain.size(), 1u) << "buyers get the status only";
    EXPECT_TRUE(plain.contains("ok"));
}

// ---- live feed (T-FR-10) ------------------------------------------------------------------------------------------

TEST_F(E4Test, LiveFeedIsFilteredByRoleAndNeverReportsARolledBackSale) {
    build_world();
    const i64 pool = make_open_pool();
    deposit(pool, farmer[0], 600000);
    deposit(pool, farmer[1], 400000);

    WsClient mws(port(), mgr.cookie), fa(port(), farmer_c[0].cookie), fb(port(), farmer_c[1].cookie), b1(port(), buyer_c[0].cookie), b2(port(), buyer_c[1].cookie);
    ASSERT_TRUE(mws.connected() && fa.connected() && fb.connected() && b1.connected() && b2.connected());
    std::this_thread::sleep_for(200ms);
    // the deposits above may still be on their way through the feed; a real client re-fetches on connect, so drop that backlog
    for (WsClient* w : {&mws, &fa, &fb, &b1, &b2}) w->collect(300);

    ASSERT_EQ(sale(buyer_c[0], pool, 500000).status, 200);
    const auto manager_msgs = mws.collect(700);
    const auto farmer_a = fa.collect(300);
    const auto farmer_b = fb.collect(300);
    const auto buyer_1 = b1.collect(300);
    const auto buyer_2 = b2.collect(300);

    // Manager: everything
    ASSERT_EQ(of_type(manager_msgs, "stock_changed").size(), 1u);
    EXPECT_EQ(of_type(manager_msgs, "stock_changed")[0]["stock_g"], 500000);
    EXPECT_EQ(of_type(manager_msgs, "stock_changed")[0]["pool_id"], pool);
    EXPECT_EQ(ledgers_of(manager_msgs), (std::set<std::string>{"inventory", "ownership", "financial"}));
    EXPECT_TRUE(of_type(manager_msgs, "reconciliation")[0]["ok"].get<bool>());
    EXPECT_TRUE(of_type(manager_msgs, "my_purchase").empty());
    for (const json& m : of_type(manager_msgs, "ledger_rows"))
        if (m["ledger"] == "financial") { EXPECT_EQ(m["rows"].size(), 3u) << "gross plus two payouts"; }

    // Farmers: only their own ownership and money rows, never the stock ledger
    EXPECT_EQ(ledgers_of(farmer_a), (std::set<std::string>{"ownership", "financial"}));
    for (const json& m : of_type(farmer_a, "ledger_rows"))
        for (const json& row : m["rows"]) EXPECT_EQ(row["seller_id"], farmer[0].id) << "farmer A got another farmer's row";
    for (const json& m : of_type(farmer_b, "ledger_rows"))
        for (const json& row : m["rows"]) EXPECT_EQ(row["seller_id"], farmer[1].id);
    EXPECT_EQ(of_type(farmer_a, "stock_changed").size(), 1u);
    EXPECT_TRUE(of_type(farmer_a, "my_purchase").empty());

    // Buyers: stock, status; the buyer who bought also gets their receipt; nobody gets ledgers
    EXPECT_EQ(of_type(buyer_1, "stock_changed").size(), 1u);
    EXPECT_TRUE(ledgers_of(buyer_1).empty());
    ASSERT_EQ(of_type(buyer_1, "my_purchase").size(), 1u);
    EXPECT_EQ(of_type(buyer_1, "my_purchase")[0]["sale"]["gross_paise"], 1250000);
    EXPECT_FALSE(of_type(buyer_1, "my_purchase")[0]["sale"].contains("buyer_id"));
    EXPECT_EQ(of_type(buyer_2, "stock_changed").size(), 1u);
    EXPECT_TRUE(ledgers_of(buyer_2).empty());
    EXPECT_TRUE(of_type(buyer_2, "my_purchase").empty()) << "only the buyer who bought is told";
    for (const auto* msgs : {&buyer_1, &buyer_2, &farmer_a, &farmer_b})
        for (const json& m : *msgs) EXPECT_FALSE(has_key(m, {"payouts"}));
    for (const auto* msgs : {&buyer_1, &buyer_2})
        for (const json& m : *msgs)
            for (const char* name : {"Zorbax", "Quillon", "Wexley"}) EXPECT_EQ(m.dump().find(name), std::string::npos) << "a buyer was sent a farmer's name";
    for (const json& m : farmer_a) for (const char* other : {"Quillon", "Wexley"}) EXPECT_EQ(m.dump().find(other), std::string::npos) << "farmer A was sent another farmer's name";
    for (const json& m : farmer_b) for (const char* other : {"Zorbax", "Wexley"}) EXPECT_EQ(m.dump().find(other), std::string::npos) << "farmer B was sent another farmer's name";

    // a price change reaches everyone; the Manager's stock feed gets a "change" row
    ASSERT_EQ(mgr.patch(P(pool), {{"price_paise_per_kg", 2400}}).status, 200);
    const auto m2 = mws.collect(500);
    const auto b1b = b1.collect(300);
    ASSERT_EQ(of_type(b1b, "price_changed").size(), 1u);
    EXPECT_EQ(of_type(b1b, "price_changed")[0]["old_price_paise_per_kg"], 2500);
    EXPECT_EQ(of_type(b1b, "price_changed")[0]["new_price_paise_per_kg"], 2400);
    ASSERT_EQ(of_type(m2, "price_changed").size(), 1u);
    bool change_row = false;
    for (const json& m : of_type(m2, "ledger_rows"))
        for (const json& row : m["rows"]) if (row.value("kind", "") == "change" && row["field"] == "PRICE") change_row = true;
    EXPECT_TRUE(change_row);

    // a refused sale changes nothing and pushes nothing
    fa.collect(200); fb.collect(200); b2.collect(200);
    EXPECT_EQ(sale(buyer_c[1], pool, 900000, 2400).status, 409);
    EXPECT_TRUE(mws.collect(500).empty());
    EXPECT_TRUE(b1.collect(100).empty());
    EXPECT_TRUE(b2.collect(100).empty());

    // a closed pool's status change reaches buyers too
    ASSERT_EQ(mgr.patch(P(pool), {{"status", "closed"}}).status, 200);
    const auto closed = b2.collect(400);
    ASSERT_EQ(of_type(closed, "status_changed").size(), 1u);
    EXPECT_EQ(of_type(closed, "status_changed")[0]["status"], "closed");
}

TEST_F(E4Test, DraftPoolActivityIsInvisibleToBuyersOnTheLiveFeed) {
    build_world();
    const i64 draft = make_draft_pool();
    WsClient mws(port(), mgr.cookie), b(port(), buyer_c[0].cookie);
    ASSERT_TRUE(mws.connected() && b.connected());
    std::this_thread::sleep_for(200ms);
    deposit(draft, farmer[0], 300000);
    EXPECT_FALSE(of_type(mws.collect(500), "stock_changed").empty());
    EXPECT_TRUE(b.collect(400).empty()) << "buyers hear nothing about draft pools";
}

TEST_F(E4Test, WebSocketHandshakeIsRefusedWithoutASessionOrFromAnotherSite) {
    build_world();
    EXPECT_FALSE(WsClient(port(), "").connected());
    EXPECT_FALSE(WsClient(port(), "agri_session=" + std::string(64, 'b')).connected());
    EXPECT_FALSE(WsClient(port(), buyer_c[0].cookie, "http://evil.example").connected()) << "cross-site WebSocket hijacking";
    EXPECT_TRUE(WsClient(port(), buyer_c[0].cookie, "http://127.0.0.1:" + std::to_string(port())).connected());
    EXPECT_TRUE(WsClient(port(), buyer_c[0].cookie).connected()) << "a non-browser client without Origin is fine";

    const std::string fresh = unique_name("firstlogin");
    server->create_account("First Login", fresh, "BUYER", kTemp);
    Client c = anon();
    ASSERT_EQ(c.login(fresh, kTemp).status, 200);
    EXPECT_FALSE(WsClient(port(), c.cookie).connected()) << "no live data before the temporary password is changed";
}

// ---- session hygiene and cross-site protection -------------------------------------------------------------------

TEST_F(E4Test, LogoutAndPasswordChangeEndLiveSocketsAndOtherSessions) {
    build_world();
    // logout closes that session's WebSocket at once
    Client a = session_of(farmer[0]);
    WsClient wa(port(), a.cookie);
    ASSERT_TRUE(wa.connected());
    std::this_thread::sleep_for(150ms);
    ASSERT_EQ(a.post_empty("/auth/logout").status, 200);
    wa.collect(400);
    EXPECT_TRUE(wa.closed()) << "a logged-out session must not keep receiving live data";

    // changing the password ends every other session of that user, and their sockets
    Client one = session_of(farmer[1]);
    Client two = session_of(farmer[1]);
    WsClient w2(port(), two.cookie);
    ASSERT_TRUE(w2.connected());
    std::this_thread::sleep_for(150ms);
    ASSERT_EQ(two.get("/pools").status, 200);
    ASSERT_EQ(one.post("/auth/change-password", {{"current_password", kPass}, {"new_password", "An0ther-Secret-pw"}}).status, 200);
    EXPECT_EQ(one.get("/pools").status, 200) << "the session that changed the password stays";
    EXPECT_EQ(two.get("/pools").status, 401) << "every other session ended";
    w2.collect(400);
    EXPECT_TRUE(w2.closed());
}

TEST_F(E4Test, StateChangingRequestsFromAnotherSiteAreRefused) {
    build_world();
    const i64 pool = make_worked_example_pool();
    Client evil = session_of(buyer[0]);
    evil.extra_headers = {{"Origin", "http://evil.example"}};
    const auto before = snapshot(pool);
    EXPECT_EQ(sale(evil, pool, 100000).status, 403);
    EXPECT_EQ(sale(evil, pool, 100000).j()["error"], "BAD_ORIGIN");
    EXPECT_EQ(evil.post_empty("/auth/logout").status, 403) << "even calls with no body";
    EXPECT_EQ(snapshot(pool), before);
    EXPECT_EQ(anon().get("/pools").status, 401);
    Client login_evil = anon();
    login_evil.extra_headers = {{"Origin", "http://evil.example"}};
    EXPECT_EQ(login_evil.post("/auth/login", {{"username", buyer[0].username}, {"password", kPass}}).status, 403);

    Client same = session_of(buyer[0]);
    same.extra_headers = {{"Origin", "http://127.0.0.1:" + std::to_string(port())}};
    EXPECT_EQ(sale(same, pool, 100000).status, 200) << "the site's own pages are fine";
    EXPECT_EQ(evil.get("/pools").status, 200) << "reads are unaffected (SameSite already keeps cookies away)";
}

TEST_F(E4Test, LoginIsAlsoThrottledPerAddressAcrossManyUsernames) {
    e4::Config c;
    c.db_conninfo = app_conninfo();
    c.port = 0;
    c.photo_dir = photo_dir;
    c.http_threads = 2;
    c.db_pool_size = 2;
    c.pwhash_opslimit = 1;
    c.pwhash_memlimit = 8192;
    c.login_max_failures = 5;
    c.login_max_failures_per_ip = 3;
    e4::Server small(c);
    small.start();
    Client x(small.port());
    for (int i = 0; i < 3; ++i) EXPECT_EQ(x.login(unique_name("guess"), "nope-nope-nope").status, 401);
    const Response r = x.login(unique_name("fresh-name"), "nope-nope-nope");
    EXPECT_EQ(r.status, 429) << "credential stuffing with a new username each time is stopped by address";
    EXPECT_TRUE(r.headers.count("retry-after"));
    small.stop();
}

// ---- static frontend ---------------------------------------------------------------------------------------------

TEST_F(E4Test, StaticFilesAreServedStrictlyAndNothingEscapesTheWebFolder) {
    // the real files, no login needed, with the right types and a locked-down policy
    const Response css = anon().get("/css/tokens.css");
    ASSERT_EQ(css.status, 200);
    EXPECT_EQ(css.headers.at("content-type"), "text/css; charset=utf-8");
    EXPECT_EQ(css.headers.at("x-content-type-options"), "nosniff");
    EXPECT_NE(css.headers.at("content-security-policy").find("default-src 'none'"), std::string::npos);
    EXPECT_NE(css.headers.at("content-security-policy").find("script-src 'self'"), std::string::npos);
    EXPECT_EQ(css.headers.at("content-security-policy").find("unsafe-inline"), std::string::npos);
    EXPECT_EQ(css.headers.at("x-frame-options"), "DENY");
    EXPECT_NE(css.body.find("--leaf"), std::string::npos);
    EXPECT_EQ(anon().get("/fonts/source-serif-4-latin-600-normal.woff2").headers.at("content-type"), "font/woff2");
    EXPECT_EQ(anon().get("/js/format.js").headers.at("content-type"), "text/javascript; charset=utf-8");
    EXPECT_EQ(anon().get("/i18n/en.json").status, 200);
    EXPECT_EQ(anon().get("/styleguide.html").headers.at("content-type"), "text/html; charset=utf-8");

    // anything that could leave the web folder or reach hidden or non-web files is a plain 404
    for (const char* bad : {"/../CLAUDE.md", "/css/../../CLAUDE.md", "/%2e%2e/CLAUDE.md", "/css/%2e%2e/%2e%2e/CLAUDE.md", "/..%2fCLAUDE.md",
                            "/.git/config", "/css/.hidden", "/.env", "/db/schema.sql", "/CMakeLists.txt", "/css", "/css/", "/js/pages",
                            "/x.php", "/css/tokens.css%00.png", "/css\\tokens.css", "//etc/passwd", "/fonts/OFL-NotoSans.txt/../../../CLAUDE.md"})
        EXPECT_EQ(anon().get(bad).status, 404) << bad;
    EXPECT_EQ(anon().post("/css/tokens.css", json::object()).status, 404);
    EXPECT_EQ(anon().get("/nope").status, 404);
    // the API is not reachable through the static handler, and still needs a login
    EXPECT_EQ(anon().get("/pools").status, 401);
    EXPECT_EQ(anon().get("/users").status, 401);
}

TEST_F(E4Test, ManyRequestsOnOneConnectionAllGetTheirOwnAnswer) {
    // a browser loads a page's files down one connection; a catch-all handler that ended its response twice once made every
    // second request on a connection come back as a plain 404
    const std::vector<std::string> paths = {"/css/tokens.css", "/css/base.css", "/nope", "/css/components.css", "/js/format.js", "/pools",
                                            "/i18n/en.json", "/js/ui.js", "/pools", "/css/fonts.css"};
    const auto responses = testhttp::request_many(port(), paths);
    ASSERT_EQ(responses.size(), paths.size());
    const int expected[] = {200, 200, 404, 200, 200, 401, 200, 200, 401, 200};
    for (std::size_t i = 0; i < paths.size(); ++i) {
        EXPECT_EQ(responses[i].status, expected[i]) << paths[i];
        if (expected[i] == 200) { EXPECT_FALSE(responses[i].body.empty()) << paths[i]; }
    }
    EXPECT_NE(responses[3].body.find(".btn"), std::string::npos) << "the right body came back for the right request";
    EXPECT_NE(responses[4].body.find("formatPaise"), std::string::npos);
}

TEST_F(E4Test, WhoAmIAnswersEveryRoleAndRefusesAnonymous) {
    build_world();
    EXPECT_EQ(anon().get("/auth/me").status, 401);
    const struct { Client* c; const Person* p; const char* role; } cases[] = {{&mgr, &manager, "manager"}, {&farmer_c[0], &farmer[0], "farmer"}, {&buyer_c[0], &buyer[0], "buyer"}};
    for (const auto& k : cases) {
        const json me = k.c->get("/auth/me").j();
        EXPECT_EQ(me["role"], k.role);
        EXPECT_EQ(me["user_id"], k.p->id);
        EXPECT_EQ(me["name"], k.p->name);
        EXPECT_FALSE(me["must_change_password"].get<bool>());
        EXPECT_FALSE(has_key(me, {"password_hash", "password", "username"}));
    }
    // still answers while the temporary password is unchanged (the login screen needs it to show the change-password step)
    const std::string fresh = unique_name("tmp");
    server->create_account("Temp Person", fresh, "BUYER", kTemp);
    Client c = anon();
    ASSERT_EQ(c.login(fresh, kTemp).status, 200);
    const Response r = c.get("/auth/me");
    EXPECT_EQ(r.status, 200);
    EXPECT_TRUE(r.j()["must_change_password"].get<bool>());
}

// ---- sign-up, approval and the people tools (decision N, 21 Nov) --------------------------------------------------------
namespace {
std::string unique_phone() {   // a valid 10-digit mobile number that no other test has used
    static std::atomic<unsigned long> n{0};
    std::random_device rd;
    return "9" + std::to_string(100000000 + (rd() % 800000) * 1000 + (n++ % 1000)).substr(0, 9);
}
json signup_body(const std::string& type, const std::string& phone, const std::string& name = "Self Signed") {
    return {{"name", name}, {"phone", phone}, {"type", type}, {"password", kPass},
            {"id_doc_type", type == "FARMER" ? "LAND_RECORD" : "GST"}, {"id_last4", "4821"}};
}
}  // namespace

TEST_F(E4Test, SignUpMakesAPendingAccount_ApprovalUnlocksDepositsAndPurchases) {
    build_world();
    const i64 pool = make_worked_example_pool();

    // a farmer signs up: signed in at once, pending, with a generated ID
    Client f = anon();
    const std::string fphone = unique_phone();
    const Response su = f.post("/auth/signup", signup_body("FARMER", fphone, "Pending Farmer"));
    ASSERT_EQ(su.status, 201) << su.body;
    EXPECT_EQ(su.j()["role"], "farmer");
    EXPECT_EQ(su.j()["approval"], "PENDING");
    ASSERT_FALSE(su.set_cookies.empty());
    f.cookie = su.set_cookies[0].substr(0, su.set_cookies[0].find(';'));
    const json me = f.get("/auth/me").j();
    EXPECT_EQ(me["approval"], "PENDING");
    EXPECT_FALSE(me["must_change_password"].get<bool>()) << "they chose their own password";
    EXPECT_EQ(me["code"].get<std::string>().substr(0, 2), "F-");
    const i64 fid = me["user_id"].get<i64>();

    // the Manager sees them as pending, with the document type and last four characters, never a password hash
    json listed;
    for (const json& u : mgr.get("/users").j()) if (u["user_id"].get<i64>() == fid) listed = u;
    ASSERT_FALSE(listed.is_null());
    EXPECT_EQ(listed["status"], "pending");
    EXPECT_EQ(listed["phone"], fphone);
    EXPECT_EQ(listed["id_doc_type"], "LAND_RECORD");
    EXPECT_EQ(listed["id_last4"], "4821");
    EXPECT_TRUE(listed["verified_at"].is_null());
    EXPECT_EQ(listed.dump().find("argon2"), std::string::npos);

    // no deposits until approved
    Response dep = mgr.post(P(pool) + "/contributions", {{"seller_id", fid}, {"quantity_g", 100000}});
    EXPECT_EQ(dep.status, 409);
    EXPECT_EQ(dep.j()["error"], "FARMER_NOT_APPROVED");
    ASSERT_EQ(mgr.post("/users/" + std::to_string(fid) + "/approve", json::object()).status, 200);
    EXPECT_EQ(mgr.post(P(pool) + "/contributions", {{"seller_id", fid}, {"quantity_g", 100000}}).status, 201);
    for (const json& u : mgr.get("/users").j())
        if (u["user_id"].get<i64>() == fid) { EXPECT_EQ(u["status"], "active"); EXPECT_EQ(u["verified_by"], "Pool Manager"); EXPECT_FALSE(u["verified_at"].is_null()); }
    EXPECT_EQ(f.get("/auth/me").j()["approval"], "APPROVED");

    // a buyer signs up: can look at pools but not buy until approved
    Client b = anon();
    const std::string bphone = unique_phone();
    const Response bs = b.post("/auth/signup", signup_body("BUYER", bphone, "Pending Buyer"));
    ASSERT_EQ(bs.status, 201) << bs.body;
    b.cookie = bs.set_cookies[0].substr(0, bs.set_cookies[0].find(';'));
    const i64 bid = b.get("/auth/me").j()["user_id"].get<i64>();
    EXPECT_EQ(b.get("/pools").status, 200);
    const Response refused = sale(b, pool, 100000);
    EXPECT_EQ(refused.status, 409);
    EXPECT_EQ(refused.j()["error"], "BUYER_NOT_APPROVED");
    ASSERT_EQ(mgr.post("/users/" + std::to_string(bid) + "/approve", json::object()).status, 200);
    const Response bought = sale(b, pool, 100000);
    EXPECT_EQ(bought.status, 200) << bought.body;

    // the mobile number signs in, on a fresh client
    Client again = anon();
    EXPECT_EQ(again.login(fphone, kPass).status, 200);
}

TEST_F(E4Test, SignUpRefusesBadInput_NoSelfMadeManagers_NoDuplicateMobileNumbers) {
    build_world();
    const std::string phone = unique_phone();
    Client c = anon();
    EXPECT_EQ(c.post("/auth/signup", signup_body("MANAGER", phone)).status, 400) << "nobody signs themselves up as a Pool Manager";
    json wrong_doc = signup_body("FARMER", phone); wrong_doc["id_doc_type"] = "GST";
    EXPECT_EQ(c.post("/auth/signup", wrong_doc).status, 400) << "a farmer cannot use a trade document";
    json short_pw = signup_body("BUYER", phone); short_pw["password"] = "short";
    EXPECT_EQ(c.post("/auth/signup", short_pw).status, 400);
    json bad_phone = signup_body("BUYER", "1234567890");
    EXPECT_EQ(c.post("/auth/signup", bad_phone).status, 400) << "must start with 6, 7, 8 or 9";
    json bad_last4 = signup_body("BUYER", phone); bad_last4["id_last4"] = "12 4";
    EXPECT_EQ(c.post("/auth/signup", bad_last4).status, 400);
    json no_doc = signup_body("BUYER", phone); no_doc.erase("id_doc_type");
    EXPECT_EQ(c.post("/auth/signup", no_doc).status, 400);
    EXPECT_EQ(c.post_raw("/auth/signup", "name=x", "application/x-www-form-urlencoded").status, 415);
    Client hostile = anon(); hostile.extra_headers = {{"Origin", "https://evil.example"}};
    EXPECT_EQ(hostile.post("/auth/signup", signup_body("BUYER", phone)).status, 403);

    ASSERT_EQ(c.post("/auth/signup", signup_body("BUYER", phone)).status, 201);
    const Response dup = anon().post("/auth/signup", signup_body("FARMER", phone));
    EXPECT_EQ(dup.status, 409);
    EXPECT_EQ(dup.j()["error"], "DUPLICATE_PHONE");
    EXPECT_EQ(mgr.post("/users", {{"name", "X"}, {"phone", phone}, {"type", "FARMER"}, {"temporary_password", kTemp}}).status, 409);
}

TEST_F(E4Test, SignUpIsThrottledPerAddress) {
    e4::Config c;
    c.db_conninfo = app_conninfo();
    c.port = 0;
    c.photo_dir = photo_dir;
    c.web_dir = AGRI_WEB_DIR;
    c.pwhash_opslimit = 1; c.pwhash_memlimit = 8192;
    c.signup_max_attempts_per_hour = 3;
    e4::Server small(c);
    small.start();
    for (int i = 0; i < 3; ++i) EXPECT_EQ(Client(small.port()).post("/auth/signup", signup_body("BUYER", unique_phone())).status, 201) << i;
    EXPECT_EQ(Client(small.port()).post("/auth/signup", signup_body("BUYER", unique_phone())).status, 429);
    small.stop();
}

TEST_F(E4Test, PeopleToolsAreForThePoolManagerOnly_DeactivateKeepsHistory) {
    build_world();
    const i64 pool = make_worked_example_pool();
    const std::string path = "/users/" + std::to_string(farmer[0].id);
    for (Client* c : {&farmer_c[1], &buyer_c[0]}) {
        EXPECT_EQ(c->post(path + "/approve", json::object()).status, 403);
        EXPECT_EQ(c->post(path + "/deactivate", json::object()).status, 403);
        EXPECT_EQ(c->post(path + "/reactivate", json::object()).status, 403);
        EXPECT_EQ(c->patch(path, {{"name", "Changed"}}).status, 403);
    }
    Client nobody = anon();
    EXPECT_EQ(nobody.post(path + "/deactivate", json::object()).status, 401);
    EXPECT_EQ(mgr.post("/users/" + std::to_string(manager.id) + "/deactivate", json::object()).status, 404) << "a Pool Manager cannot be changed here";
    EXPECT_EQ(mgr.post("/users/999999999/approve", json::object()).status, 404);

    // deactivating ends the session and the sign-in, but the ledgers keep the person
    EXPECT_EQ(farmer_c[0].get("/me/pools").status, 200);
    ASSERT_EQ(mgr.post(path + "/deactivate", json::object()).status, 200);
    EXPECT_EQ(farmer_c[0].get("/me/pools").status, 401);
    EXPECT_EQ(anon().login(farmer[0].username, kPass).status, 401);
    EXPECT_EQ(mgr.post(P(pool) + "/contributions", {{"seller_id", farmer[0].id}, {"quantity_g", 100000}}).status, 409);
    const json statement = mgr.get("/farmers/" + std::to_string(farmer[0].id) + "/statement?pool=" + std::to_string(pool)).j();
    EXPECT_EQ(statement["summary"]["deposited_g"], 600000) << "the 600 kg deposit is still on record";
    ASSERT_EQ(mgr.post(path + "/reactivate", json::object()).status, 200);
    EXPECT_EQ(anon().login(farmer[0].username, kPass).status, 200);

    // editing: name, mobile number (unique), document (a changed document is checked again)
    EXPECT_EQ(mgr.patch(path, json::object()).status, 400);
    EXPECT_EQ(mgr.patch(path, {{"name", "Renamed Farmer"}}).status, 200);
    const std::string phone = unique_phone();
    EXPECT_EQ(mgr.patch(path, {{"phone", phone}}).status, 200);
    EXPECT_EQ(mgr.patch("/users/" + std::to_string(farmer[1].id), {{"phone", phone}}).status, 409);
    EXPECT_EQ(mgr.patch(path, {{"phone", "12345"}}).status, 400);
    const std::string as_username = unique_phone();
    ASSERT_EQ(mgr.post("/users", {{"name", "Digits Name"}, {"username", as_username}, {"type", "BUYER"}, {"temporary_password", kTemp}}).status, 201);
    EXPECT_EQ(mgr.patch(path, {{"phone", as_username}}).status, 409) << "another person already signs in with that number";
    EXPECT_EQ(mgr.patch(path, {{"id_doc_type", "PAN"}}).status, 400) << "type and last four go together";
    EXPECT_EQ(mgr.post(path + "/approve", json::object()).status, 409) << "no identity document is recorded yet";
    ASSERT_EQ(mgr.patch(path, {{"id_doc_type", "AADHAAR"}, {"id_last4", "0042"}}).status, 200);
    for (const json& u : mgr.get("/users").j())
        if (u["user_id"].get<i64>() == farmer[0].id) { EXPECT_EQ(u["name"], "Renamed Farmer"); EXPECT_EQ(u["status"], "pending"); EXPECT_EQ(u["id_last4"], "0042"); }
    ASSERT_EQ(mgr.post(path + "/approve", json::object()).status, 200);
    EXPECT_EQ(farmer_c[0].get("/me/pools").status, 401) << "the old session was ended by the deactivation";
}

TEST_F(E4Test, PoolMarketIsStoredAndShown) {
    build_world();
    const Response r = mgr.post("/pools", {{"commodity_id", paddy_id()}, {"variety", "Sona Masuri"}, {"range_no", 1}, {"storage_location", "Test godown"},
                                           {"market", "Hubballi APMC"}, {"min_order_g", 100000}, {"price_paise_per_kg", 2500}});
    ASSERT_EQ(r.status, 201) << r.body;
    const i64 pool = r.j()["pool_id"];
    EXPECT_EQ(mgr.get(P(pool)).j()["market"], "Hubballi APMC");
    EXPECT_TRUE(mgr.get(P(make_draft_pool())).j()["market"].is_null());
    EXPECT_EQ(mgr.post("/pools", {{"commodity_id", paddy_id()}, {"variety", "X"}, {"range_no", 1}, {"storage_location", "g"}, {"market", ""},
                                  {"min_order_g", 100000}, {"price_paise_per_kg", 2500}}).status, 400);
}

// ---- market prices (decision Q, 25 Nov) ---------------------------------------------------------------------------------
namespace {
json quote(const std::string& district, const std::string& market, const std::string& commodity, const std::string& variety, const std::string& grade,
           const std::string& date, const json& lo, const json& hi, const json& modal) {
    return {{"state", "Karnataka"}, {"district", district}, {"market", market}, {"commodity", commodity}, {"variety", variety}, {"grade", grade},
            {"arrival_date", date}, {"min_price", lo}, {"max_price", hi}, {"modal_price", modal}};
}
json sample_prices(int shift = 0) {   // shift moves every modal price, to test that a second update replaces the first
    return json::array({
        quote("Haveri", "Ranebennur APMC", "Paddy(Common)", "Paddy", "FAQ", "25/11/2026", 3700, 3800, 3750 + shift),
        quote("Davangere", "Davangere APMC", "Paddy(Common)", "Paddy Medium", "Local", "25/11/2026", 3000, 3900, 3481 + shift),
        quote("Vijayanagara", "H.B. Halli APMC", "Paddy(Common)", "Sona Mahsuri", "FAQ", "25/11/2026", 3800, 4000, 3900 + shift),
        quote("Gadag", "Gadag APMC", "Maize", "Local", "FAQ", "25/11/2026", 2900, 3300, 3078 + shift),
        quote("Haveri", "Haveri APMC", "Maize", "Local", "FAQ", "25/11/2026", 3100, 3100, 3100 + shift),
        quote("Gadag", "Rona APMC", "Red gram/Arhar/Tur(whole)", "Arhar (Whole)", "Non-FAQ", "25/11/2026", 8800, 9400, 9150 + shift),
        quote("Gadag", "Gadag APMC", "Red gram split/Arhar dal/Tur dal", "Arhar Dal(Tur)", "Medium", "25/11/2026", 13000, 14000, 13470 + shift),
        quote("Gadag", "Gadag APMC", "Groundnut", "Gejje", "FAQ", "25/11/2026", 6000, 8000, 7454 + shift),
        quote("Raichur", "Raichur APMC", "Groundnut", "Shelled (Bunch)", "FAQ", "25/11/2026", 7000, 7600, 7369 + shift),   // kernels: ignored
        quote("Dharwad", "APMC Hubballi", "Potato", "Potato", "Local", "25/11/2026", 1400, 1900, 1500),                  // not one of our crops: ignored
        quote("Haveri", "Hanagal APMC", "Paddy(Common)", "Paddy", "FAQ", "25/11/2026", 2725.5, 3300, 2900),                // a fraction: refused
        quote("Haveri", "Hanagal APMC", "Maize", "Local", "FAQ", "2026-11-25", 2900, 3100, 3000),                         // a date that is not day/month/year: refused
        quote("Haveri", "Hanagal APMC", "Maize", "Hybrid", "FAQ", "25/11/2026", 3500, 3000, 3200),                        // maximum below minimum: refused
        quote("Haveri", "Hanagal APMC", "Maize", "Brand New Variety", "FAQ", "25/11/2026", "2800", "3000", "2900"),       // digit strings are fine
    });
}
}  // namespace

TEST_F(E4Test, MarketPricesAreUpdatedByTheManagerOnly_AndSummarisedWithIntegers) {
    build_world();
    feed_down = false;
    feed_records = sample_prices();
    EXPECT_EQ(anon().post("/market-prices/refresh", json::object()).status, 401);
    EXPECT_EQ(farmer_c[0].post("/market-prices/refresh", json::object()).status, 403);
    EXPECT_EQ(buyer_c[0].post("/market-prices/refresh", json::object()).status, 403);
    const Response ok = mgr.post("/market-prices/refresh", json::object());
    ASSERT_EQ(ok.status, 200) << ok.body;
    EXPECT_EQ(ok.j()["imported"], 9) << "3 paddy, 3 maize, 2 tur, 1 groundnut; the 5 refused or unrelated rows are not stored";

    const i64 paddy = paddy_id();
    EXPECT_EQ(anon().get("/market-prices?commodity_id=" + std::to_string(paddy)).status, 401);
    EXPECT_EQ(mgr.get("/market-prices").status, 400);
    for (Client* c : {&mgr, &farmer_c[0], &buyer_c[0]}) EXPECT_EQ(c->get("/market-prices?commodity_id=" + std::to_string(paddy)).status, 200) << "every signed-in role may read prices";
    const json all = mgr.get("/market-prices?commodity_id=" + std::to_string(paddy)).j();
    EXPECT_EQ(all["as_of"], "2026-11-25");
    EXPECT_EQ(all["source"], "Agmarknet via data.gov.in");
    EXPECT_EQ(all["commodity"]["quotes"], 3);
    EXPECT_EQ(all["commodity"]["markets"], 3);
    EXPECT_EQ(all["commodity"]["median_modal_paise_per_kg"], 3750);
    EXPECT_EQ(all["commodity"]["lowest_min_paise_per_kg"], 3000);
    EXPECT_EQ(all["commodity"]["highest_max_paise_per_kg"], 4000);
    EXPECT_TRUE(all["variety"].is_null());
    EXPECT_EQ(all["quotes"].size(), 3u);
    const json mine = mgr.get("/market-prices?commodity_id=" + std::to_string(paddy) + "&variety=Sona%20Mahsuri").j();
    EXPECT_EQ(mine["variety"]["quotes"], 1);
    EXPECT_EQ(mine["variety"]["median_modal_paise_per_kg"], 3900);
    EXPECT_EQ(mine["quotes"][0]["variety"], "Sona Mahsuri") << "the chosen variety is listed first";
    EXPECT_EQ(mine["quotes"][0]["market"], "H.B. Halli APMC");
    EXPECT_FALSE(has_key(mine, {"is_variety"}));
    // three maize quotes: the middle one is the median
    json maize;
    for (const json& c : mgr.get("/reference/commodities").j()) if (c["name"] == "Maize") maize = c;
    const json m = mgr.get("/market-prices?commodity_id=" + std::to_string(maize["commodity_id"].get<i64>())).j();
    EXPECT_EQ(m["commodity"]["quotes"], 3);
    EXPECT_EQ(m["commodity"]["median_modal_paise_per_kg"], 3078 + 0) << "modal prices 2900, 3078, 3100: the middle one";
    // the Tur commodity holds both whole and split tur; kernels of groundnut are left out
    json tur, nut;
    for (const json& c : mgr.get("/reference/commodities").j()) { if (c["name"] == "Tur (Arhar)") tur = c; if (c["name"] == "Groundnut with pods") nut = c; }
    EXPECT_EQ(mgr.get("/market-prices?commodity_id=" + std::to_string(tur["commodity_id"].get<i64>())).j()["commodity"]["quotes"], 2);
    EXPECT_EQ(mgr.get("/market-prices?commodity_id=" + std::to_string(nut["commodity_id"].get<i64>())).j()["commodity"]["quotes"], 1);
    // new varieties reach the Create pool drop-down
    const auto& varieties = maize["varieties"];
    EXPECT_NE(std::find(varieties.begin(), varieties.end(), "Brand New Variety"), varieties.end());
    EXPECT_NE(std::find(varieties.begin(), varieties.end(), "Local"), varieties.end());
}

TEST_F(E4Test, ASecondUpdateReplacesTheDaysQuotes_AFailedUpdateKeepsTheLastOnes) {
    build_world();
    feed_down = false;
    feed_records = sample_prices();
    ASSERT_EQ(mgr.post("/market-prices/refresh", json::object()).status, 200);
    const std::string path = "/market-prices?commodity_id=" + std::to_string(paddy_id());
    EXPECT_EQ(mgr.get(path).j()["commodity"]["median_modal_paise_per_kg"], 3750);
    feed_records = sample_prices(100);   // prices moved later the same day
    ASSERT_EQ(mgr.post("/market-prices/refresh", json::object()).status, 200);
    const json after = mgr.get(path).j();
    EXPECT_EQ(after["commodity"]["quotes"], 3) << "the same quotes were updated, not added again";
    EXPECT_EQ(after["commodity"]["median_modal_paise_per_kg"], 3850);

    feed_down = true;
    const Response failed = mgr.post("/market-prices/refresh", json::object());
    EXPECT_EQ(failed.status, 502);
    EXPECT_EQ(failed.j()["error"], "PRICE_FEED_UNAVAILABLE");
    EXPECT_EQ(failed.body.find("down"), std::string::npos) << "the browser gets a code, never the reason text";
    EXPECT_EQ(mgr.get(path).j()["commodity"]["median_modal_paise_per_kg"], 3850) << "the last saved prices stay";
    feed_down = false;
    feed_records = json::array({"not an object", json::object()});   // a file nobody can use is not stored and changes nothing
    EXPECT_EQ(mgr.post("/market-prices/refresh", json::object()).j()["imported"], 0);
    EXPECT_EQ(mgr.get(path).j()["commodity"]["median_modal_paise_per_kg"], 3850);
}

TEST_F(E4Test, MarketPriceUpdatesAreNotSetUpWithoutAKey_AndAreRateLimited) {
    e4::Config c;
    c.db_conninfo = app_conninfo();
    c.port = 0;
    c.photo_dir = photo_dir;
    c.web_dir = AGRI_WEB_DIR;
    c.pwhash_opslimit = 1; c.pwhash_memlimit = 8192;
    {   // no key and no fake feed: the button says it is not set up, and nothing is run
        e4::Server bare(c);
        bare.start();
        const std::string user = unique_name("mgr");
        bare.create_account("Manager", user, "MANAGER", kTemp);
        Client m(bare.port());
        ASSERT_EQ(m.login(user, kTemp).status, 200);
        ASSERT_EQ(m.post("/auth/change-password", {{"current_password", kTemp}, {"new_password", kPass}}).status, 200);
        const Response r = m.post("/market-prices/refresh", json::object());
        EXPECT_EQ(r.status, 503);
        EXPECT_EQ(r.j()["error"], "PRICE_FEED_NOT_SET_UP");
        bare.stop();
    }
    c.market_feed = [](const std::string&, int, int) { return json{{"total", 0}, {"count", 0}, {"records", json::array()}}.dump(); };
    c.market_min_seconds_between_updates = 3600;
    e4::Server slow(c);
    slow.start();
    const std::string user = unique_name("mgr");
    slow.create_account("Manager", user, "MANAGER", kTemp);
    Client m(slow.port());
    ASSERT_EQ(m.login(user, kTemp).status, 200);
    ASSERT_EQ(m.post("/auth/change-password", {{"current_password", kTemp}, {"new_password", kPass}}).status, 200);
    EXPECT_EQ(m.post("/market-prices/refresh", json::object()).status, 200);
    const Response again = m.post("/market-prices/refresh", json::object());
    EXPECT_EQ(again.status, 429) << "pressing the button twice quickly makes one request to the shared service";
    EXPECT_EQ(again.j()["error"], "TOO_SOON");
    slow.stop();
}
