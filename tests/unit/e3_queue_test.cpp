// E3 queue tests with a fake executor (no database). Covers FIFO order, one-at-a-time per pool, parallel pools,
// retry and BUSY, queue timeout, back-pressure, shutdown, exceptions, and a fake-E2 stock race (50 buyers x 20 runs).
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "e3/pool_queue.hpp"

using namespace std::chrono_literals;

namespace {

struct Job { int pool; int id; int qty = 1; };
struct Res { int id = -1; bool transient = false; bool busy = false; bool accepted = false; int attempts = 0; int stock_after = 0; };

using Queue = e3::PoolQueue<Job, Res>;

Queue::Callbacks callbacks(std::function<Res(const Job&)> attempt) {
    Queue::Callbacks cb;
    cb.key = [](const Job& j) { return static_cast<std::int64_t>(j.pool); };
    cb.attempt = std::move(attempt);
    cb.is_transient = [](const Res& r) { return r.transient; };
    cb.busy = [] { Res r; r.busy = true; return r; };
    return cb;
}

}  // namespace

TEST(E3Queue, SamePoolRunsInArrivalOrder) {
    std::vector<int> order;   // only touched by the single active job of pool 1, so no lock is needed if the queue is right
    Queue q(callbacks([&](const Job& j) { order.push_back(j.id); Res r; r.id = j.id; return r; }), {4, 3, 1ms, 5000ms, 10000});
    std::vector<std::future<Res>> fs;
    for (int i = 0; i < 300; ++i) fs.push_back(q.submit({1, i}));
    for (auto& f : fs) f.get();
    ASSERT_EQ(order.size(), 300u);
    for (int i = 0; i < 300; ++i) ASSERT_EQ(order[i], i);
}

TEST(E3Queue, NeverTwoJobsOfOnePoolAtTheSameTime) {
    std::atomic<int> running{0}, max_running{0};
    Queue q(callbacks([&](const Job&) {
        const int now = ++running;
        int seen = max_running.load();
        while (now > seen && !max_running.compare_exchange_weak(seen, now)) {}
        std::this_thread::sleep_for(1ms);
        --running;
        return Res{};
    }), {8, 3, 1ms, 5000ms, 10000});
    std::vector<std::thread> submitters;
    std::vector<std::future<Res>> fs(80);
    std::mutex m;
    for (int t = 0; t < 8; ++t)
        submitters.emplace_back([&, t] {
            for (int i = 0; i < 10; ++i) { auto f = q.submit({7, t * 10 + i}); std::lock_guard<std::mutex> l(m); fs[t * 10 + i] = std::move(f); }
        });
    for (auto& s : submitters) s.join();
    for (auto& f : fs) f.get();
    EXPECT_EQ(max_running.load(), 1);
}

TEST(E3Queue, DifferentPoolsRunInParallel) {
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::promise<void> pool2_done;
    Queue q(callbacks([&](const Job& j) {
        if (j.pool == 1) gate.wait();             // pool 1 is stuck until released
        else pool2_done.set_value();
        return Res{};
    }), {2, 3, 1ms, 5000ms, 10000});
    auto stuck = q.submit({1, 0});
    auto other = q.submit({2, 0});
    EXPECT_EQ(pool2_done.get_future().wait_for(2s), std::future_status::ready) << "pool 2 must not wait for pool 1";
    other.get();
    release.set_value();
    stuck.get();
}

TEST(E3Queue, TransientResultIsRetriedThenSucceeds) {
    std::atomic<int> calls{0};
    Queue q(callbacks([&](const Job& j) { Res r; r.id = j.id; r.attempts = ++calls; r.transient = calls < 3; return r; }),
            {2, 3, 10ms, 5000ms, 10000});
    const auto t0 = std::chrono::steady_clock::now();
    const Res r = q.submit({1, 5}).get();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_FALSE(r.busy);
    EXPECT_EQ(r.attempts, 3);
    EXPECT_GE(elapsed, 30ms);   // back-off 10 ms then 20 ms
    EXPECT_EQ(q.stats().retried, 2u);
    EXPECT_EQ(q.stats().completed, 1u);
}

TEST(E3Queue, RetriesStopAfterThreeAttemptsAndReportBusy) {
    std::atomic<int> calls{0};
    Queue q(callbacks([&](const Job&) { ++calls; Res r; r.transient = true; return r; }), {2, 3, 1ms, 5000ms, 10000});
    const Res r = q.submit({1, 0}).get();
    EXPECT_TRUE(r.busy);
    EXPECT_EQ(calls.load(), 3);
    EXPECT_EQ(q.stats().busy, 1u);
}

TEST(E3Queue, ExceptionReachesTheCallerAndTheLaneKeepsWorking) {
    Queue q(callbacks([&](const Job& j) -> Res { if (j.id == 1) throw std::runtime_error("boom"); Res r; r.id = j.id; return r; }),
            {1, 3, 1ms, 5000ms, 10000});
    auto a = q.submit({1, 0});
    auto b = q.submit({1, 1});
    auto c = q.submit({1, 2});
    EXPECT_EQ(a.get().id, 0);
    EXPECT_THROW(b.get(), std::runtime_error);
    EXPECT_EQ(c.get().id, 2);
    EXPECT_EQ(q.stats().failed, 1u);
}

TEST(E3Queue, JobsThatWaitedTooLongAreBusyAndNeverRun) {
    std::atomic<int> ran{0};
    Queue q(callbacks([&](const Job& j) { ++ran; if (j.id == 0) std::this_thread::sleep_for(200ms); return Res{}; }),
            {1, 3, 1ms, 50ms, 10000});
    auto slow = q.submit({1, 0});
    auto late = q.submit({1, 1});
    EXPECT_FALSE(slow.get().busy);
    EXPECT_TRUE(late.get().busy);
    EXPECT_EQ(ran.load(), 1);
}

TEST(E3Queue, FullQueueAnswersBusyAtOnce) {
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<bool> started{false};
    Queue q(callbacks([&](const Job&) { started = true; gate.wait(); return Res{}; }), {1, 3, 1ms, 5000ms, 2});
    auto first = q.submit({1, 0});
    while (!started) std::this_thread::sleep_for(1ms);   // it is running, so the lane's waiting list is empty
    auto w1 = q.submit({1, 1});
    auto w2 = q.submit({1, 2});
    auto over = q.submit({1, 3});   // third waiting job: beyond max_queue_per_key = 2
    EXPECT_EQ(over.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(over.get().busy);
    release.set_value();
    EXPECT_FALSE(first.get().busy);
    EXPECT_FALSE(w1.get().busy);
    EXPECT_FALSE(w2.get().busy);
}

TEST(E3Queue, StopFinishesTheRunningJobAndAnswersBusyToTheRest) {
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<bool> started{false};
    std::atomic<int> ran{0};
    auto q = std::make_unique<Queue>(callbacks([&](const Job&) { ++ran; started = true; gate.wait(); Res r; r.accepted = true; return r; }),
                                     e3::Options{1, 3, 1ms, 5000ms, 10000});
    auto running = q->submit({1, 0});
    while (!started) std::this_thread::sleep_for(1ms);
    auto w1 = q->submit({1, 1});
    auto w2 = q->submit({1, 2});
    std::thread stopper([&] { q->stop(); });
    EXPECT_TRUE(w1.get().busy);
    EXPECT_TRUE(w2.get().busy);
    release.set_value();
    stopper.join();
    EXPECT_TRUE(running.get().accepted);
    EXPECT_EQ(ran.load(), 1);
    EXPECT_TRUE(q->submit({1, 9}).get().busy);   // after stop everything is BUSY
}

// A fake E2: plain, unprotected stock arithmetic. It is only correct because E3 never runs two jobs of one pool at once.
TEST(E3Queue, FakeSaleStress_50BuyersTwentyRuns_ZeroOversold) {
    for (int run = 0; run < 20; ++run) {
        int stock = 10;
        int accepted = 0, rejected = 0;
        Queue q(callbacks([&](const Job& j) {
            Res r;
            r.id = j.id;
            std::this_thread::yield();
            if (j.qty <= stock) { stock -= j.qty; r.accepted = true; }
            r.stock_after = stock;
            return r;
        }), {4, 3, 1ms, 5000ms, 10000});
        std::vector<std::future<Res>> fs;
        for (int b = 0; b < 50; ++b) fs.push_back(q.submit({1, b, 1}));
        std::vector<int> accepted_ids;
        for (auto& f : fs) { const Res r = f.get(); if (r.accepted) { ++accepted; accepted_ids.push_back(r.id); } else ++rejected; }
        ASSERT_EQ(accepted, 10) << "run " << run;
        ASSERT_EQ(accepted + rejected, 50);
        ASSERT_EQ(stock, 0);
        for (int i = 0; i < 10; ++i) ASSERT_EQ(accepted_ids[i], i) << "the first ten arrivals win";   // FIFO fairness
    }
}
