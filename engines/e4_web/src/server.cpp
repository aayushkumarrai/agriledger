#include "e4/server.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>

#include "e1/hashing.hpp"
#include "e3/e2_adapter.hpp"
#include "live.hpp"
#include "photos.hpp"
#include "support.hpp"

namespace e4 {
namespace {

constexpr int kManager = 1, kFarmer = 2, kBuyer = 4, kAny = 7;
constexpr i64 kMaxGrams = 1000000000000000LL;
constexpr i64 kMaxPrice = 1000000000LL;
const char* const kCookie = "agri_session";

struct Principal {
    e2::SessionInfo s;
    e2::Viewer viewer;
    int role_bit = 0;
};

int status_for(const std::string& code) {
    if (code == "NOT_FOUND") return 404;
    if (code == "INVALID") return 400;
    return 409;   // POOL_NOT_OPEN, NO_CHANGE, INVALID_TRANSITION, PHOTO_REQUIRED, DUPLICATE_USERNAME, REQUEST_ID_CONFLICT
}

crow::response map_current_exception() {
    try {
        throw;
    } catch (const HttpError& e) {
        return error_response(e.status, e.code);
    } catch (const e2::DomainError& e) {
        return error_response(status_for(e.code()), e.code());
    } catch (const e2::TransientError&) {
        crow::response r = error_response(503, "BUSY");
        r.set_header("Retry-After", "1");
        return r;
    } catch (const e2::ReconciliationFailed& e) {
        std::cerr << "ALERT: " << e.what() << " (the transaction was rolled back)\n";
        return error_response(500, "INTERNAL");
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";   // details stay in the server log
        return error_response(500, "INTERNAL");
    }
}

std::string session_token_from(const crow::request& req) {
    const std::string& header = req.get_header_value("Cookie");
    std::size_t pos = 0;
    const std::string prefix = std::string(kCookie) + "=";
    while (pos < header.size()) {
        std::size_t end = header.find(';', pos);
        if (end == std::string::npos) end = header.size();
        std::size_t a = pos;
        while (a < end && header[a] == ' ') ++a;
        const std::string part = header.substr(a, end - a);
        if (part.compare(0, prefix.size(), prefix) == 0) {
            const std::string v = part.substr(prefix.size());
            if (v.size() == 64 && std::all_of(v.begin(), v.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
                return v;
        }
        pos = end + 1;
    }
    return "";
}

std::optional<i64> parse_id(const char* s) {
    if (!s || !*s) return std::nullopt;
    i64 v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9' || v > 100000000000000LL) return std::nullopt;
        v = v * 10 + (*p - '0');
    }
    return v;
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

json receipt_json(const e2::Receipt& r, bool with_payouts) {
    json j = {{"status", "ACCEPTED"}, {"duplicate", r.duplicate}, {"sale_id", r.sale_id}, {"receipt_no", r.receipt_no}, {"pool_id", r.pool_id},
              {"quantity_g", r.quantity_g}, {"unit_price_paise_per_kg", r.unit_price_paise_per_kg}, {"gross_paise", r.gross_paise},
              {"created_at", r.created_at}, {"storage_location", r.storage_location}, {"stock_after_g", r.stock_after_g}};
    if (with_payouts) {   // the Manager only: buyers never receive farmers, shares or payouts
        j["payouts"] = json::array();
        for (const e2::PayoutRow& p : r.payouts) j["payouts"].push_back({{"seller_id", p.seller_id}, {"alloc_g", p.alloc_g}, {"paise", p.paise}});
    }
    return j;
}

}  // namespace

struct Server::Impl {
    Config cfg;
    e2::Engine engine;
    e3::SaleQueue queue;
    PasswordHasher hasher;
    LoginLimiter limiter;
    LoginLimiter ip_limiter;
    LiveHub hub;
    crow::SimpleApp app;
    std::future<void> running;
    bool started = false;

    static e2::Config engine_config(const Config& c) {
        e2::Config e;
        e.conninfo = c.db_conninfo;
        e.pool_size = c.db_pool_size;
        e.lock_timeout_ms = c.lock_timeout_ms;
        return e;
    }

    explicit Impl(Config c)
        : cfg(std::move(c)), engine(engine_config(cfg)), queue(e3::sale_callbacks(engine), cfg.queue),
          hasher(cfg.pwhash_opslimit, cfg.pwhash_memlimit), limiter(cfg.login_max_failures, cfg.login_window_seconds),
          ip_limiter(cfg.login_max_failures_per_ip, cfg.login_window_seconds), hub(engine, cfg.db_conninfo) {
        crow::logger::setLogLevel(crow::LogLevel::Warning);
    }

    // ---- authentication: every request, on the server --------------------------------------------------------------

    std::optional<Principal> authenticate(const crow::request& req) {
        const std::string token = session_token_from(req);
        if (token.empty()) return std::nullopt;
        const auto s = engine.find_session(token);
        if (!s) return std::nullopt;
        Principal p;
        p.s = *s;
        p.viewer.user_id = s->user_id;
        if (s->type == "MANAGER") { p.viewer.role = e2::Viewer::Role::Manager; p.role_bit = kManager; }
        else if (s->type == "FARMER") { p.viewer.role = e2::Viewer::Role::Farmer; p.role_bit = kFarmer; }
        else { p.viewer.role = e2::Viewer::Role::Buyer; p.role_bit = kBuyer; }
        return p;
    }

    // 401 without a valid session; 403 while the temporary password is unchanged (except change-password and logout);
    // 403 for a role the route does not allow; domain and internal errors become plain reason codes.
    template <class F>
    crow::response guarded(const crow::request& req, int allowed_roles, F&& handler, bool password_change_exempt = false) {
        try {
            if (req.method != crow::HTTPMethod::Get && !same_origin(req)) return error_response(403, "BAD_ORIGIN");   // cross-site request
            const auto p = authenticate(req);
            if (!p) return error_response(401, "UNAUTHENTICATED");
            if (p->s.must_change_password && !password_change_exempt) return error_response(403, "PASSWORD_CHANGE_REQUIRED");
            if (!(allowed_roles & p->role_bit)) return error_response(403, "FORBIDDEN");
            return handler(*p);
        } catch (...) {
            return map_current_exception();
        }
    }

    std::string cookie_header(const std::string& token, int max_age) const {
        return std::string(kCookie) + "=" + token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=" + std::to_string(max_age) +
               (cfg.cookie_secure ? "; Secure" : "");
    }

    // ---- routes ----------------------------------------------------------------------------------------------------

    void routes() {
        // Auth ------------------------------------------------------------------------------------------------------
        CROW_ROUTE(app, "/auth/login").methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            try {
                if (!same_origin(req)) return error_response(403, "BAD_ORIGIN");
                const json j = body_object(req);
                const std::string username = need_str(j, "username", 1, 100), password = need_str(j, "password", 1, 200);
                const std::string key = req.remote_ip_address + "|" + username;
                if (limiter.blocked(key) || ip_limiter.blocked(req.remote_ip_address)) {
                    crow::response r = error_response(429, "TOO_MANY_ATTEMPTS");
                    r.set_header("Retry-After", std::to_string(limiter.window_seconds()));
                    return r;
                }
                const auto user = engine.find_user(username);
                bool ok = false;
                if (user && user->is_active) ok = hasher.verify(user->password_hash, password);
                else hasher.burn_time(password);   // an unknown name takes as long as a wrong password
                if (!ok) {
                    limiter.failure(key);
                    ip_limiter.failure(req.remote_ip_address);
                    return error_response(401, "INVALID_CREDENTIALS");   // one generic answer for every wrong detail
                }
                limiter.success(key);
                const std::string token = new_session_token();
                engine.create_session(user->user_id, token, cfg.session_ttl_seconds);
                std::string role = user->type;
                std::transform(role.begin(), role.end(), role.begin(), [](unsigned char c) { return std::tolower(c); });
                crow::response r = json_response(200, {{"role", role}, {"must_change_password", user->must_change_password}, {"name", user->name}});
                r.add_header("Set-Cookie", cookie_header(token, cfg.session_ttl_seconds));
                return r;
            } catch (...) {
                return map_current_exception();
            }
        });

        CROW_ROUTE(app, "/auth/logout").methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return guarded(req, kAny, [&](const Principal&) {
                const std::string token = session_token_from(req);
                engine.delete_session(token);
                hub.close_token(token);   // its live sockets end with it
                crow::response r = json_response(200, json::object());
                r.add_header("Set-Cookie", cookie_header("", 0));
                return r;
            }, /*password_change_exempt=*/true);
        });

        CROW_ROUTE(app, "/auth/change-password").methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return guarded(req, kAny, [&](const Principal& p) {
                const json j = body_object(req);
                const std::string current = need_str(j, "current_password", 1, 200), next = need_str(j, "new_password", 8, 200);
                const std::string key = "pw|" + p.s.username;
                if (limiter.blocked(key)) return error_response(429, "TOO_MANY_ATTEMPTS");
                const auto user = engine.find_user(p.s.username);
                if (!user || !hasher.verify(user->password_hash, current)) {
                    limiter.failure(key);
                    return error_response(403, "WRONG_PASSWORD");
                }
                limiter.success(key);
                if (next == current) return error_response(400, "SAME_PASSWORD");
                engine.set_password(p.s.user_id, hasher.hash(next));
                engine.delete_other_sessions(p.s.user_id, session_token_from(req));   // a changed password ends every other session
                hub.sweep();
                return json_response(200, {{"must_change_password", false}});
            }, /*password_change_exempt=*/true);
        });

        // Accounts --------------------------------------------------------------------------------------------------
        CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([this](const crow::request& req) {
            return guarded(req, kManager, [&](const Principal&) {
                if (req.method == crow::HTTPMethod::Get) return json_response(200, engine.list_users());
                const json j = body_object(req);
                const std::string type = need_str(j, "type", 1, 10);
                if (type != "FARMER" && type != "BUYER") throw HttpError(400, "INVALID_FIELD");   // no way to create a Manager through the API
                const std::string name = need_str(j, "name", 1, 100), username = need_str(j, "username", 3, 50);
                const std::string temporary = need_str(j, "temporary_password", 8, 200);
                const i64 id = engine.create_user({name, username, type, hasher.hash(temporary)});
                return json_response(201, {{"user_id", id}});
            });
        });

        // Reference data --------------------------------------------------------------------------------------------
        CROW_ROUTE(app, "/reference/commodities").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return guarded(req, kAny, [&](const Principal&) { return json_response(200, engine.commodities()); });
        });
        CROW_ROUTE(app, "/reference/prices").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return guarded(req, kAny, [&](const Principal&) { return json_response(200, engine.reference_prices()); });
        });

        // Pools -----------------------------------------------------------------------------------------------------
        CROW_ROUTE(app, "/pools").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([this](const crow::request& req) {
            return guarded(req, kAny, [&](const Principal& p) {
                if (req.method == crow::HTTPMethod::Get) return json_response(200, engine.list_pools(p.viewer));
                if (p.role_bit != kManager) return error_response(403, "FORBIDDEN");
                const json j = body_object(req);
                e2::NewPool np;
                np.commodity_id = need_int(j, "commodity_id", 1, kMaxGrams);
                np.variety = need_str(j, "variety", 1, 60);
                np.range_no = static_cast<int>(need_int(j, "range_no", 1, 3));
                np.packaging = opt_str(j, "packaging", 60);
                np.storage_location = need_str(j, "storage_location", 1, 120);
                np.min_order_g = need_int(j, "min_order_g", 1, kMaxGrams);
                np.price_paise_per_kg = need_int(j, "price_paise_per_kg", 1, kMaxPrice);   // whole paise per kg = whole rupees per quintal
                return json_response(201, {{"pool_id", engine.create_pool(np, p.s.user_id)}});
            });
        });

        CROW_ROUTE(app, "/pools/<int>").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Patch)([this](const crow::request& req, int id) {
            return guarded(req, kAny, [&](const Principal& p) {
                if (req.method == crow::HTTPMethod::Get) return json_response(200, engine.pool_detail(id, p.viewer));
                if (p.role_bit != kManager) return error_response(403, "FORBIDDEN");
                const json j = body_object(req);
                const auto price = opt_int(j, "price_paise_per_kg", 1, kMaxPrice);
                const auto status = opt_str(j, "status", 10);
                if (!price && !status) throw HttpError(400, "MISSING_FIELD");
                if (status && *status != "open" && *status != "closed") throw HttpError(400, "INVALID_FIELD");
                if (price) engine.change_price(id, *price, p.s.user_id);
                if (status) engine.change_status(id, *status, p.s.user_id);
                return json_response(200, engine.pool_detail(id, p.viewer));
            });
        });

        CROW_ROUTE(app, "/pools/<int>/photos").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([this](const crow::request& req, int id) {
            return guarded(req, kAny, [&](const Principal& p) {
                if (req.method == crow::HTTPMethod::Get) return json_response(200, engine.pool_photos(id, p.viewer));
                if (p.role_bit != kManager) return error_response(403, "FORBIDDEN");   // only the Pool Manager uploads photos
                return upload_photo(req, id, p);
            });
        });

        CROW_ROUTE(app, "/pools/<int>/contributions").methods(crow::HTTPMethod::Post)([this](const crow::request& req, int id) {
            return guarded(req, kManager, [&](const Principal& p) {
                const json j = body_object(req);
                const i64 seller = need_int(j, "seller_id", 1, kMaxGrams), quantity = need_int(j, "quantity_g", 1, kMaxGrams);
                const e2::ContributionResult r = engine.record_contribution(id, seller, quantity, p.s.user_id);
                return json_response(201, {{"event_id", r.event_id}, {"stock_after_g", r.stock_after_g}, {"seller_balance_g", r.seller_balance_g}});
            });
        });

        CROW_ROUTE(app, "/pools/<int>/sales").methods(crow::HTTPMethod::Post)([this](const crow::request& req, int id) {
            return guarded(req, kBuyer | kManager, [&](const Principal& p) { return place_sale(req, id, p); });
        });

        CROW_ROUTE(app, "/pools/<int>/ledgers/<string>").methods(crow::HTTPMethod::Get)([this](const crow::request& req, int id, const std::string& kind) {
            return guarded(req, kManager, [&](const Principal&) {
                std::optional<i64> before;
                if (const char* b = req.url_params.get("before")) {
                    before = parse_id(b);
                    if (!before) throw HttpError(400, "INVALID_FIELD");
                }
                i64 limit = 50;
                if (const char* l = req.url_params.get("limit")) {
                    const auto v = parse_id(l);
                    if (!v || *v < 1 || *v > 200) throw HttpError(400, "INVALID_FIELD");
                    limit = *v;
                }
                return json_response(200, engine.ledger_page(id, kind, before, static_cast<int>(limit)));
            });
        });

        CROW_ROUTE(app, "/pools/<int>/reconciliation").methods(crow::HTTPMethod::Get)([this](const crow::request& req, int id) {
            return guarded(req, kAny, [&](const Principal& p) {
                if (p.role_bit != kManager) return json_response(200, {{"ok", engine.pool_detail(id, p.viewer)["records_ok"]}});   // status only
                const e2::ReconciliationStatus st = engine.reconciliation(id);
                json failures = json::array();
                for (const auto& f : st.failures) failures.push_back({{"invariant", f.invariant}, {"event_id", f.event_id}});
                return json_response(200, {{"ok", st.ok}, {"failures", failures}});
            });
        });

        CROW_ROUTE(app, "/pools/<int>/integrity-check").methods(crow::HTTPMethod::Post)([this](const crow::request& req, int id) {
            return guarded(req, kManager, [&](const Principal&) {
                const e2::IntegrityResult r = engine.integrity_check(id);
                json j = {{"ok", r.ok}, {"checked_entries", r.checked_entries}};
                if (r.first_broken)
                    j["first_broken"] = {{"sequence_no", r.first_broken->sequence_no}, {"event_type", r.first_broken->event_type}, {"reason", r.first_broken->reason}};
                return json_response(200, j);
            });
        });

        // Farmers and buyers ----------------------------------------------------------------------------------------
        CROW_ROUTE(app, "/me/pools").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return guarded(req, kFarmer, [&](const Principal& p) { return json_response(200, engine.my_pools(p.s.user_id)); });
        });
        CROW_ROUTE(app, "/me/purchases").methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return guarded(req, kBuyer, [&](const Principal& p) { return json_response(200, engine.my_purchases(p.s.user_id)); });   // own receipts only
        });
        CROW_ROUTE(app, "/farmers/<int>/statement").methods(crow::HTTPMethod::Get)([this](const crow::request& req, int farmer_id) {
            return guarded(req, kManager | kFarmer, [&](const Principal& p) {
                if (p.role_bit == kFarmer && p.s.user_id != farmer_id) return error_response(403, "FORBIDDEN");   // that farmer only
                const auto pool = parse_id(req.url_params.get("pool"));
                if (!pool) throw HttpError(400, "MISSING_FIELD");
                return json_response(200, engine.farmer_statement(farmer_id, *pool));
            });
        });

        // Photos (any logged-in user; names are content hashes) ----------------------------------------------------
        CROW_ROUTE(app, "/photos/<string>").methods(crow::HTTPMethod::Get)([this](const crow::request& req, const std::string& name) {
            return guarded(req, kAny, [&](const Principal&) {
                const auto bytes = read_photo(cfg.photo_dir, name);
                if (!bytes) return error_response(404, "NOT_FOUND");
                crow::response r(200);
                r.body = *bytes;
                r.set_header("Content-Type", name.size() > 3 && name.compare(name.size() - 3, 3, "png") == 0 ? "image/png" : "image/jpeg");
                r.set_header("X-Content-Type-Options", "nosniff");
                r.set_header("Cache-Control", "private, max-age=86400, immutable");
                return r;
            });
        });

        // Everything else: the static frontend (or a plain 404)
        CROW_CATCHALL_ROUTE(app)([this](const crow::request& req, crow::response& res) {
            // Crow calls res.end() itself after a catch-all handler; ending it here too would complete the response twice
            // and desynchronise a keep-alive connection (every second request on it came back as a plain 404).
            try { res = serve_static(req); } catch (...) { res = map_current_exception(); }
        });

        // Live feed -------------------------------------------------------------------------------------------------
        CROW_WEBSOCKET_ROUTE(app, "/live")
            .onaccept([this](const crow::request& req, void** userdata) {
                try {
                    // refuse cross-site WebSocket hijacking: a browser's Origin must be this site
                    const std::string& origin = req.get_header_value("Origin");
                    if (!origin.empty()) {
                        const auto scheme = origin.find("://");
                        const std::string host = scheme == std::string::npos ? origin : origin.substr(scheme + 3);
                        if (!iequals(host, req.get_header_value("Host"))) return false;
                    }
                    const auto p = authenticate(req);
                    if (!p || p->s.must_change_password) return false;
                    *userdata = new ConnInfo{p->viewer, session_token_from(req)};
                    return true;
                } catch (...) {
                    return false;
                }
            })
            .onopen([this](crow::websocket::connection& c) {
                if (auto* info = static_cast<ConnInfo*>(c.userdata())) hub.add(c, *info);
            })
            .onclose([this](crow::websocket::connection& c, const std::string&, uint16_t) {
                hub.remove(c);
                delete static_cast<ConnInfo*>(c.userdata());
                c.userdata(nullptr);
            })
            .onmessage([](crow::websocket::connection&, const std::string&, bool) { /* clients send nothing */ });
    }

    // ---- static frontend -----------------------------------------------------------------------------------------

    // Serves web_dir. Only plain file names made of letters, digits, dot, dash and underscore, no hidden files, and only
    // known file types; anything else, or anything that resolves outside web_dir, is a plain 404. No API data is ever
    // served from here, and a strict Content-Security-Policy keeps pages to their own scripts, styles and fonts.
    crow::response serve_static(const crow::request& req) {
        namespace fs = std::filesystem;
        if (req.method != crow::HTTPMethod::Get) return error_response(404, "NOT_FOUND");
        std::string url = req.url == "/" ? "/index.html" : req.url;
        if (url.size() > 200 || url[0] != '/') return error_response(404, "NOT_FOUND");
        for (char c : url) if (c == '%' || c == '\\' || c == ':' || c == '\0') return error_response(404, "NOT_FOUND");
        fs::path rel;
        std::size_t pos = 1;
        while (pos <= url.size()) {
            std::size_t end = url.find('/', pos);
            if (end == std::string::npos) end = url.size();
            const std::string part = url.substr(pos, end - pos);
            const bool plain = !part.empty() && part[0] != '.' &&
                               std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; });
            if (!plain) return error_response(404, "NOT_FOUND");
            rel /= part;
            pos = end + 1;
        }
        static const std::map<std::string, std::string> types = {
            {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
            {".json", "application/json; charset=utf-8"}, {".woff2", "font/woff2"}, {".svg", "image/svg+xml"},
            {".png", "image/png"}, {".ico", "image/x-icon"}, {".txt", "text/plain; charset=utf-8"}};
        const auto type = types.find(rel.extension().string());
        if (type == types.end()) return error_response(404, "NOT_FOUND");
        std::error_code ec;
        const fs::path base = fs::weakly_canonical(cfg.web_dir, ec);
        const fs::path full = fs::weakly_canonical(base / rel, ec);
        if (ec || full.string().compare(0, base.string().size() + 1, base.string() + "/") != 0 || !fs::is_regular_file(full, ec))
            return error_response(404, "NOT_FOUND");   // also catches a symlink that points outside web_dir
        std::ifstream in(full, std::ios::binary);
        if (!in) return error_response(404, "NOT_FOUND");
        crow::response r(200);
        r.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        r.set_header("Content-Type", type->second);
        r.set_header("X-Content-Type-Options", "nosniff");
        r.set_header("X-Frame-Options", "DENY");
        r.set_header("Referrer-Policy", "same-origin");
        r.set_header("Content-Security-Policy",
                     "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; "
                     "connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
        r.set_header("Cache-Control", rel.extension() == ".woff2" ? "public, max-age=31536000, immutable" : "no-cache");
        return r;
    }

    // ---- handlers with more than a few lines ---------------------------------------------------------------------

    crow::response place_sale(const crow::request& req, int pool_id, const Principal& p) {
        const json j = body_object(req);
        e2::SaleRequest sr;
        sr.request_id = need_str(j, "request_id", 36, 36);
        sr.pool_id = pool_id;
        sr.quantity_g = need_int(j, "quantity_g", 1, kMaxGrams);
        sr.confirmed_price_paise_per_kg = need_int(j, "confirmed_price_paise_per_kg", 1, kMaxPrice);
        if (p.role_bit == kBuyer) {
            // a Buyer buys for themselves: the server uses the session user, never a value from the body
            const auto claimed = opt_int(j, "buyer_id", 1, kMaxGrams);
            if (claimed && *claimed != p.s.user_id) return error_response(403, "FORBIDDEN");
            sr.buyer_id = sr.actor_user_id = p.s.user_id;
        } else {
            sr.buyer_id = need_int(j, "buyer_id", 1, kMaxGrams);
            sr.actor_user_id = p.s.user_id;   // recorded as who placed the sale
        }

        const e2::SaleOutcome o = queue.submit(sr).get();   // E4 -> E3 (per-pool FIFO) -> E2; rethrows exceptions from E2
        switch (o.status) {
            case e2::SaleOutcome::Status::Accepted:
                return json_response(200, receipt_json(o.receipt, p.role_bit == kManager));
            case e2::SaleOutcome::Status::Rejected: {
                json j2 = {{"status", "REJECTED"}, {"reason", e2::to_string(o.reason)}};
                if (o.reason == e2::Reason::InsufficientStock) j2["stock_g"] = o.stock_g;
                if (o.reason == e2::Reason::PriceChanged) j2["current_price_paise_per_kg"] = o.current_price_paise_per_kg;
                if (o.reason == e2::Reason::BelowMinOrder) j2["min_order_g"] = o.min_order_g;
                return json_response(409, j2);
            }
            default: {
                crow::response r = json_response(503, {{"status", "REJECTED"}, {"reason", "BUSY"}});
                r.set_header("Retry-After", "1");
                return r;
            }
        }
    }

    crow::response upload_photo(const crow::request& req, int pool_id, const Principal& p) {
        if (req.get_header_value("Content-Type").compare(0, 19, "multipart/form-data") != 0) throw HttpError(415, "UNSUPPORTED_MEDIA_TYPE");
        if (req.body.size() > cfg.max_photo_bytes + 64 * 1024) throw HttpError(413, "PHOTO_TOO_LARGE");   // Crow has already buffered it; refuse to go further
        std::string bytes;
        try {
            crow::multipart::message msg(req);
            bytes = msg.get_part_by_name("photo").body;
        } catch (const std::exception&) {
            throw HttpError(400, "BAD_UPLOAD");
        }
        if (bytes.empty()) throw HttpError(400, "BAD_UPLOAD");
        if (bytes.size() > cfg.max_photo_bytes) throw HttpError(413, "PHOTO_TOO_LARGE");
        const auto ext = sniff_image_extension(bytes);   // by content, not by the name or type the client claims
        if (!ext) throw HttpError(415, "UNSUPPORTED_IMAGE");
        engine.pool_photos(pool_id, p.viewer);   // the pool must exist (NOT_FOUND) before anything is written
        const std::string sha = e1::sha256_hex(bytes);
        if (!store_photo(cfg.photo_dir, sha, *ext, bytes)) throw std::runtime_error("could not store the photo");
        const std::string path = "photos/" + sha + "." + *ext;
        const i64 photo_id = engine.add_photo(pool_id, p.s.user_id, path, sha);
        return json_response(201, {{"photo_id", photo_id}, {"url", "/" + path}});
    }
};

Server::Server(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Server::~Server() { stop(); }

void Server::start() {
    if (impl_->started) return;
    impl_->routes();
    impl_->app.websocket_max_payload(4096).bindaddr(impl_->cfg.bind_address).port(impl_->cfg.port).concurrency(static_cast<unsigned>(impl_->cfg.http_threads));
    impl_->running = impl_->app.run_async();
    impl_->app.wait_for_server_start();
    impl_->hub.start();
    impl_->started = true;
}

void Server::stop() {
    if (!impl_ || !impl_->started) return;
    impl_->started = false;
    impl_->hub.stop();
    impl_->queue.stop();
    impl_->app.stop();
    if (impl_->running.valid()) impl_->running.wait();
}

std::int64_t Server::create_account(const std::string& name, const std::string& username, const std::string& type,
                                    const std::string& temporary_password) {
    return impl_->engine.create_user({name, username, type, impl_->hasher.hash(temporary_password)});
}

std::uint16_t Server::port() const { return impl_->app.port(); }
e2::Engine& Server::engine() { return impl_->engine; }

}  // namespace e4
