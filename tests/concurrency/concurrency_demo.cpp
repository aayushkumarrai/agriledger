// Prints the protected-versus-unprotected comparison for the evaluators (O6). Test tooling, not part of the server.
// Run through scripts/demo_concurrency.sh, which prepares the database and the environment variables.
#include <iostream>

#include "harness.hpp"

int main() {
    using namespace testdb;
    if (app_conninfo().empty() || super_conninfo().empty()) { std::cerr << "run scripts/demo_concurrency.sh\n"; return 2; }
    constexpr i64 kQuintal = 100000;
    e2::Config cfg;
    cfg.conninfo = app_conninfo();
    cfg.pool_size = 10;
    e2::Engine eng(cfg);
    World world(eng);
    std::cout << "50 buyers each ask for 1 quintal from a pool holding 10 quintal. 10 runs of each mode.\n\n"
              << "run  mode           accepted rejected  sold(q)  stock left(q)  oversold\n";
    for (int run = 1; run <= 10; ++run) {
        const harness::RunResult p = harness::run_protected(eng, world, 50, kQuintal, 10 * kQuintal, e3::Options{4, 3, std::chrono::milliseconds(10), std::chrono::milliseconds(10000), 10000});
        const harness::RunResult u = harness::run_unprotected(50, kQuintal, 10 * kQuintal);
        std::cout << run << "    protected      " << p.accepted << "        " << p.rejected << "       " << p.accepted_g / kQuintal << "        " << p.final_stock_g / kQuintal << "              " << (p.oversold() ? "YES" : "no") << "\n";
        std::cout << run << "    UNPROTECTED    " << u.accepted << "        " << u.rejected << "        " << u.accepted_g / kQuintal << "        " << u.final_stock_g / kQuintal << "              " << (u.oversold() ? "YES" : "no") << "\n";
    }
    harness::drop_scratch();
    return 0;
}
