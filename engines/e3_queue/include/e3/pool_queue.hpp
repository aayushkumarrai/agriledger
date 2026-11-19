// E3 Concurrency Engine: per-pool FIFO queues, worker threads, retry with back-off, BUSY (master §7, §11).
//
// Jobs with the same key (the pool id) run strictly one at a time and in arrival order; jobs for different keys run in
// parallel on the worker threads. E3 does not know what a job is: the callbacks run one attempt, say whether the result
// is transient, and build the BUSY result. An exception thrown by an attempt is handed to the caller through the future.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace e3 {

struct Options {
    std::size_t workers = 4;                                  // threads; different pools run in parallel up to this many
    int max_attempts = 3;                                     // spec: up to 3 tries for a transient failure, then BUSY
    std::chrono::milliseconds backoff{10};                    // wait before the 2nd attempt; doubles for each later one
    std::chrono::milliseconds queue_timeout{5000};            // longest a job may wait in the queue before BUSY
    std::size_t max_queue_per_key = 10000;                    // back-pressure: a longer queue answers BUSY at once
};

struct Stats {
    std::uint64_t submitted = 0, completed = 0, retried = 0, busy = 0, failed = 0;
};

template <class Job, class Result>
class PoolQueue {
public:
    struct Callbacks {
        std::function<std::int64_t(const Job&)> key;           // which queue the job belongs to
        std::function<Result(const Job&)> attempt;             // one try (E2's execute_sale, or a fake)
        std::function<bool(const Result&)> is_transient;       // true: nothing changed, trying again may work
        std::function<Result()> busy;                          // the result for BUSY
    };

    explicit PoolQueue(Callbacks callbacks, Options options = {}) : cb_(std::move(callbacks)), opt_(options) {
        if (opt_.workers == 0) opt_.workers = 1;
        if (opt_.max_attempts < 1) opt_.max_attempts = 1;
        for (std::size_t i = 0; i < opt_.workers; ++i) workers_.emplace_back([this] { worker_loop(); });
    }
    ~PoolQueue() { stop(); }
    PoolQueue(const PoolQueue&) = delete;
    PoolQueue& operator=(const PoolQueue&) = delete;

    // Queues the job. The future holds the result, or the exception the attempt threw.
    std::future<Result> submit(Job job) {
        ++submitted_;
        Item item{std::move(job), {}, Clock::now()};
        std::future<Result> future = item.promise.get_future();
        const std::int64_t k = cb_.key(item.job);
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!stop_) {
                Lane& lane = lanes_[k];
                if (lane.items.size() < opt_.max_queue_per_key) {
                    const bool wake = !lane.active && lane.items.empty();
                    lane.items.push_back(std::move(item));
                    if (wake) { ready_.push_back(k); cv_.notify_one(); }
                    return future;
                }
                if (!lane.active && lane.items.empty()) lanes_.erase(k);
            }
        }
        finish_busy(item.promise);   // stopped, or the queue is full
        return future;
    }

    // Stops accepting work, answers BUSY to everything still waiting, lets running attempts finish, joins the threads.
    void stop() {
        std::lock_guard<std::mutex> only_one_stopper(stop_m_);   // two callers must not join the same threads
        std::vector<Item> waiting;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (stop_ && workers_.empty()) return;
            stop_ = true;
            for (auto& kv : lanes_)
                while (!kv.second.items.empty()) { waiting.push_back(std::move(kv.second.items.front())); kv.second.items.pop_front(); }
            ready_.clear();
        }
        cv_.notify_all();
        for (Item& it : waiting) finish_busy(it.promise);
        for (std::thread& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
    }

    Stats stats() const {
        return Stats{submitted_.load(), completed_.load(), retried_.load(), busy_.load(), failed_.load()};
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Item { Job job; std::promise<Result> promise; Clock::time_point enqueued; };
    struct Lane { std::deque<Item> items; bool active = false; };

    void finish_busy(std::promise<Result>& p) {
        ++busy_;
        try { p.set_value(cb_.busy()); } catch (...) { p.set_exception(std::current_exception()); }
    }

    void process(Item item) {
        if (Clock::now() - item.enqueued > opt_.queue_timeout) { finish_busy(item.promise); return; }
        auto wait = opt_.backoff;
        for (int attempt = 1;; ++attempt) {
            try {
                Result r = cb_.attempt(item.job);
                if (!cb_.is_transient(r)) { ++completed_; item.promise.set_value(std::move(r)); return; }
            } catch (...) {
                ++failed_;
                item.promise.set_exception(std::current_exception());
                return;
            }
            if (attempt >= opt_.max_attempts) { finish_busy(item.promise); return; }
            ++retried_;
            std::this_thread::sleep_for(wait);   // holds this pool's lane, so arrival order is kept
            wait *= 2;
        }
    }

    void worker_loop() {
        std::unique_lock<std::mutex> lock(m_);
        for (;;) {
            cv_.wait(lock, [this] { return stop_ || !ready_.empty(); });
            if (ready_.empty()) { if (stop_) return; continue; }
            const std::int64_t k = ready_.front();
            ready_.pop_front();
            Lane& lane = lanes_[k];
            lane.active = true;
            Item item = std::move(lane.items.front());
            lane.items.pop_front();
            lock.unlock();
            process(std::move(item));
            lock.lock();
            auto it = lanes_.find(k);   // the map may have rehashed while unlocked
            it->second.active = false;
            if (!it->second.items.empty()) { ready_.push_back(k); cv_.notify_one(); }
            else lanes_.erase(it);
        }
    }

    Callbacks cb_;
    Options opt_;
    std::mutex m_;
    std::mutex stop_m_;
    std::condition_variable cv_;
    std::unordered_map<std::int64_t, Lane> lanes_;
    std::deque<std::int64_t> ready_;   // keys with waiting jobs and no active worker, in arrival order
    bool stop_ = false;
    std::vector<std::thread> workers_;
    std::atomic<std::uint64_t> submitted_{0}, completed_{0}, retried_{0}, busy_{0}, failed_{0};
};

}  // namespace e3
