// Optional glue: runs sales through E3 into E2. Include only from targets that link e2_sale (E4, the harness).
#pragma once
#include "e2/engine.hpp"
#include "e3/pool_queue.hpp"

namespace e3 {

using SaleQueue = PoolQueue<e2::SaleRequest, e2::SaleOutcome>;

// `attempt` defaults to the real sale; the test harness passes its own to inject failures.
// The engine is captured by reference: it must outlive the queue.
inline SaleQueue::Callbacks sale_callbacks(e2::Engine& engine,
                                           std::function<e2::SaleOutcome(const e2::SaleRequest&)> attempt = nullptr) {
    SaleQueue::Callbacks cb;
    cb.key = [](const e2::SaleRequest& r) { return static_cast<std::int64_t>(r.pool_id); };
    cb.attempt = attempt ? std::move(attempt) : [&engine](const e2::SaleRequest& r) { return engine.execute_sale(r); };
    cb.is_transient = [](const e2::SaleOutcome& o) { return o.status == e2::SaleOutcome::Status::Transient; };
    cb.busy = [] {
        e2::SaleOutcome o;
        o.status = e2::SaleOutcome::Status::Busy;
        return o;
    };
    return cb;
}

}  // namespace e3
