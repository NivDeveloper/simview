// The sync layer's engine: pure std, no SDL. Correctness first — the
// swap pair sits under a mutex, fine at per-frame rates; the wait-free
// refinement waits for a measurement that wants it.

#include <simview/sync/Sync.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace sv {
namespace impl {

namespace {

struct ChannelImpl {
    std::size_t bytes;
    void *state, *transfer, *draw;
    std::uint64_t transfer_gen = 0; // guarded by m
    std::uint64_t draw_gen = 0;     // consumer-thread-only
    std::uint64_t next_gen = 1;     // sim-thread-only
    std::mutex m;                   // guards the state<->transfer pair
};

// The Executor KEEPS THE CLOCK. The body may read n and time; it never
// fabricates its own counter — that is what makes a step counter, a
// time readout and "advance N" derivable rather than hand-rolled, and
// what the transport panel is built from.
struct ExecutorImpl {
    void (*tick)(void *) = nullptr;
    void (*timed)(const Tick &, void *) = nullptr;
    void *user = nullptr;
    // Restart is a STATE the worker observes at the loop head, so the
    // callback runs on the worker thread between ticks and can never
    // overlap one — the race the predecessor's restart had.
    enum class St { Paused, Playing, Step, Restart, Stopped };
    St st = St::Paused;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<std::uint64_t> delay_ns{0};
    std::atomic<std::uint64_t> ticks{0};
    // "Advance N": play until ticks reaches this, then the worker pauses
    // ITSELF. Checked >= at the loop head, so a target already passed
    // fires at once rather than never.
    std::atomic<std::uint64_t> target{0};
    std::atomic<bool> has_target{false};
    std::atomic<bool> restarted{false};
    void (*on_restart)(void *) = nullptr;
    void *restart_user = nullptr;
    double dt = 0.0;
    double time = 0.0;
    // Achieved rate: ticks completed in the last second of wall time.
    std::deque<std::chrono::steady_clock::time_point> recent;
    std::thread worker;

    void run_body() {
        if (timed) {
            const Tick t{ticks.load(std::memory_order_relaxed), time, dt};
            timed(t, user);
        } else {
            tick(user);
        }
    }
};

namespace {

void worker_loop(ExecutorImpl *e) {
    using St = ExecutorImpl::St;
    for (;;) {
        {
            std::unique_lock lk(e->m);
            e->cv.wait(lk, [e] { return e->st != St::Paused; });
            if (e->st == St::Stopped)
                return;
            if (e->st == St::Restart) {
                // Between ticks by construction: nothing is in flight.
                if (e->on_restart)
                    e->on_restart(e->restart_user);
                e->ticks.store(0, std::memory_order_release);
                e->time = 0.0;
                e->recent.clear();
                e->has_target.store(false, std::memory_order_relaxed);
                e->restarted.store(true, std::memory_order_release);
                e->st = St::Paused;
                continue;
            }
            if (e->has_target.load(std::memory_order_relaxed) &&
                e->ticks.load(std::memory_order_relaxed) >=
                    e->target.load(std::memory_order_relaxed)) {
                e->has_target.store(false, std::memory_order_relaxed);
                e->st = St::Paused;
                continue;
            }
            if (e->st == St::Step)
                e->st = St::Paused; // one-shot
        }
        e->run_body();
        {
            std::lock_guard lk(e->m);
            e->ticks.fetch_add(1, std::memory_order_release);
            e->time += e->dt;
            const auto now = std::chrono::steady_clock::now();
            e->recent.push_back(now);
            while (!e->recent.empty() &&
                   now - e->recent.front() > std::chrono::seconds(1))
                e->recent.pop_front();
        }
        if (const auto d = e->delay_ns.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::nanoseconds(d));
    }
}

} // namespace

} // namespace

Channel channel_create(std::size_t bytes) {
    if (!bytes)
        return {};
    auto *c = new ChannelImpl{bytes, ::operator new(bytes),
                              ::operator new(bytes), ::operator new(bytes)};
    std::memset(c->state, 0, bytes);
    std::memset(c->transfer, 0, bytes);
    std::memset(c->draw, 0, bytes);
    return {c};
}

void channel_destroy(Channel ch) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    if (!c)
        return;
    ::operator delete(c->state);
    ::operator delete(c->transfer);
    ::operator delete(c->draw);
    delete c;
}

void *channel_state(Channel ch) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    return c ? c->state : nullptr;
}

void channel_publish(Channel ch) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    if (!c)
        return;
    std::lock_guard lk(c->m);
    std::swap(c->state, c->transfer);
    c->transfer_gen = c->next_gen++;
}

const void *channel_latest(Channel ch, std::uint64_t *gen) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    if (!c)
        return nullptr;
    {
        std::lock_guard lk(c->m);
        if (c->transfer_gen > c->draw_gen) {
            std::swap(c->transfer, c->draw);
            c->draw_gen = c->transfer_gen;
        }
    }
    if (gen)
        *gen = c->draw_gen;
    return c->draw_gen ? c->draw : nullptr;
}

Executor executor_create(void (*tick)(void *), void *user) {
    if (!tick)
        return {};
    auto *e = new ExecutorImpl;
    e->tick = tick;
    e->user = user;
    e->worker = std::thread([e] { worker_loop(e); });
    return {e};
}

Executor executor_create_timed(void (*tick)(const Tick &, void *), void *user) {
    if (!tick)
        return {};
    auto *e = new ExecutorImpl;
    e->timed = tick;
    e->user = user;
    e->worker = std::thread([e] { worker_loop(e); });
    return {e};
}

void executor_destroy(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return;
    {
        std::lock_guard lk(e->m);
        e->st = ExecutorImpl::St::Stopped;
    }
    e->cv.notify_all();
    e->worker.join();
    delete e;
}

namespace {
void set_state(Executor ex, ExecutorImpl::St st) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return;
    {
        std::lock_guard lk(e->m);
        if (e->st == ExecutorImpl::St::Stopped)
            return;
        e->st = st;
    }
    e->cv.notify_all();
}
} // namespace

void executor_play(Executor ex) {
    if (auto *e = static_cast<ExecutorImpl *>(ex.p))
        e->has_target.store(false, std::memory_order_relaxed);
    set_state(ex, ExecutorImpl::St::Playing);
}
void executor_pause(Executor ex) { set_state(ex, ExecutorImpl::St::Paused); }
void executor_step(Executor ex) { set_state(ex, ExecutorImpl::St::Step); }

void executor_advance(Executor ex, std::uint64_t n) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e || !n)
        return;
    // Set the target BEFORE playing, so the worker cannot run past it
    // in the gap.
    e->target.store(e->ticks.load(std::memory_order_acquire) + n,
                    std::memory_order_relaxed);
    e->has_target.store(true, std::memory_order_release);
    set_state(ex, ExecutorImpl::St::Playing);
}

void executor_restart(Executor ex) { set_state(ex, ExecutorImpl::St::Restart); }

void executor_on_restart(Executor ex, void (*fn)(void *), void *user) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return;
    std::lock_guard lk(e->m);
    e->on_restart = fn;
    e->restart_user = user;
}

bool executor_restarted(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    return e && e->restarted.exchange(false, std::memory_order_acq_rel);
}

std::uint64_t executor_delay_ns(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    return e ? e->delay_ns.load(std::memory_order_relaxed) : 0;
}

void executor_set_dt(Executor ex, double dt) {
    if (auto *e = static_cast<ExecutorImpl *>(ex.p)) {
        std::lock_guard lk(e->m);
        e->dt = dt;
    }
}

Tick executor_tick(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return {};
    std::lock_guard lk(e->m);
    return {e->ticks.load(std::memory_order_relaxed), e->time, e->dt};
}

double executor_rate(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return 0.0;
    std::lock_guard lk(e->m);
    // Prune against NOW, not only when a tick arrives: a paused
    // executor stops adding timestamps, and without this the stale
    // window would report the old rate forever. Measured — it read 76
    // after a pause before this line existed.
    const auto now = std::chrono::steady_clock::now();
    while (!e->recent.empty() &&
           now - e->recent.front() > std::chrono::seconds(1))
        e->recent.pop_front();
    if (e->recent.size() < 2)
        return 0.0;
    const auto span = e->recent.back() - e->recent.front();
    const double secs = std::chrono::duration<double>(span).count();
    return secs > 0.0 ? double(e->recent.size() - 1) / secs : 0.0;
}

bool executor_playing(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e)
        return false;
    std::lock_guard lk(e->m);
    return e->st == ExecutorImpl::St::Playing;
}

void executor_set_delay_ns(Executor ex, std::uint64_t ns) {
    if (auto *e = static_cast<ExecutorImpl *>(ex.p))
        e->delay_ns.store(ns, std::memory_order_relaxed);
}

std::uint64_t executor_ticks(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    return e ? e->ticks.load(std::memory_order_acquire) : 0;
}

} // namespace impl
} // namespace sv
