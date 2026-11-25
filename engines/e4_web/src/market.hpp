// Internal to E4: fetching and reading the Agmarknet daily price file (data.gov.in), and turning it into E2 rows.
// Fetching runs the system `curl` program with fixed arguments (no shell, nothing from a user is inserted). The URL, which
// contains the API key, is given to curl on its standard input so it never appears in the process list.
#pragma once
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "e2/engine.hpp"

namespace e4 {

// Runs curl and returns the body. Throws std::runtime_error (never containing the key) if curl fails or the answer is not 200.
inline std::string curl_get(const std::string& url, int timeout_seconds) {
    int to_child[2], from_child[2];
    if (::pipe(to_child) != 0 || ::pipe(from_child) != 0) throw std::runtime_error("pipe failed");
    const pid_t pid = ::fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        ::dup2(to_child[0], 0);
        ::dup2(from_child[1], 1);
        ::close(to_child[0]); ::close(to_child[1]); ::close(from_child[0]); ::close(from_child[1]);
        const std::string t = std::to_string(timeout_seconds);
        ::execlp("curl", "curl", "-sS", "-m", t.c_str(), "-A", "AgriLedger/1.0", "-w", "\n%{http_code}", "-K", "-", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(to_child[0]); ::close(from_child[1]);
    const std::string config = "url = \"" + url + "\"\n";
    std::size_t off = 0;
    while (off < config.size()) {
        const ssize_t n = ::write(to_child[1], config.data() + off, config.size() - off);
        if (n <= 0) break;
        off += static_cast<std::size_t>(n);
    }
    ::close(to_child[1]);
    std::string out;
    std::array<char, 8192> buf{};
    for (;;) {
        const ssize_t n = ::read(from_child[0], buf.data(), buf.size());
        if (n <= 0) break;
        out.append(buf.data(), static_cast<std::size_t>(n));
        if (out.size() > 20 * 1024 * 1024) { ::kill(pid, SIGKILL); break; }   // far bigger than any real answer
    }
    ::close(from_child[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    const auto nl = out.rfind('\n');
    const std::string code = nl == std::string::npos ? "" : out.substr(nl + 1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || code != "200" || nl == std::string::npos) throw std::runtime_error("the price service did not answer");
    return out.substr(0, nl);
}

// Our commodity for the name Agmarknet uses. Anything else in the file is ignored.
inline const char* our_commodity(const std::string& source) {
    if (source == "Paddy(Common)") return "Paddy";
    if (source == "Maize") return "Maize";
    if (source == "Red gram/Arhar/Tur(whole)" || source == "Red gram split/Arhar dal/Tur dal") return "Tur (Arhar)";
    if (source == "Groundnut") return "Groundnut with pods";
    return nullptr;
}

// A whole number of rupees from a JSON number or digit string; a fraction, a negative or text is refused (integers only).
inline bool whole_rupees(const nlohmann::json& v, std::int64_t& out) {
    if (v.is_number_integer() || v.is_number_unsigned()) { out = v.get<std::int64_t>(); return out > 0 && out < 1000000000LL; }
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
        out = std::stoll(s);
        return out > 0;
    }
    return false;
}

// "25/11/2026" -> "2026-11-25"; empty if it is not a real-looking date.
inline std::string iso_date(const std::string& d) {
    if (d.size() != 10 || d[2] != '/' || d[5] != '/') return "";
    const std::string day = d.substr(0, 2), month = d.substr(3, 2), year = d.substr(6, 4);
    for (const std::string* part : {&day, &month, &year}) if (part->find_first_not_of("0123456789") != std::string::npos) return "";
    if (std::stoi(month) < 1 || std::stoi(month) > 12 || std::stoi(day) < 1 || std::stoi(day) > 31) return "";
    return year + "-" + month + "-" + day;
}

struct ParsedFeed {
    std::vector<e2::MarketPriceRow> rows;
    std::int64_t total = 0;          // how many records the service says it has
    std::size_t received = 0;        // how many records this answer held
};

inline ParsedFeed parse_feed(const std::string& text, const std::map<std::string, std::int64_t>& commodity_ids) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("records") || !j["records"].is_array()) throw std::runtime_error("the price file was not understood");
    ParsedFeed out;
    out.total = j.value("total", static_cast<std::int64_t>(0));
    out.received = j["records"].size();
    for (const auto& r : j["records"]) {
        if (!r.is_object()) continue;
        const auto text_of = [&](const char* key) { return r.contains(key) && r[key].is_string() ? r[key].get<std::string>() : std::string(); };
        const char* ours = our_commodity(text_of("commodity"));
        if (!ours) continue;
        const auto id = commodity_ids.find(ours);
        if (id == commodity_ids.end()) continue;
        std::string variety = text_of("variety");
        if (std::string(ours) == "Groundnut with pods" && variety.find("Shelled") != std::string::npos) continue;   // kernels, not pods
        e2::MarketPriceRow row;
        row.commodity_id = id->second;
        row.state = text_of("state"); row.district = text_of("district"); row.market = text_of("market");
        row.variety = variety; row.market_grade = text_of("grade");
        row.arrival_date = iso_date(text_of("arrival_date"));
        if (row.arrival_date.empty() || row.state.empty() || row.district.empty() || row.market.empty() || row.variety.empty() || row.market_grade.empty()) continue;
        if (!r.contains("min_price") || !r.contains("max_price") || !r.contains("modal_price")) continue;
        if (!whole_rupees(r["min_price"], row.min_paise_per_kg) || !whole_rupees(r["max_price"], row.max_paise_per_kg) || !whole_rupees(r["modal_price"], row.modal_paise_per_kg)) continue;
        out.rows.push_back(std::move(row));
    }
    return out;
}

}  // namespace e4
