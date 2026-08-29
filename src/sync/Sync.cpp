// The sync layer's engine: pure std, no SDL. Correctness first — the
// swap pair sits under a mutex, fine at per-frame rates; the wait-free
// refinement waits for a measurement that wants it.

#include <simview/Sync.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace sv {
namespace seam {

namespace {

struct ChannelImpl {
    std::size_t bytes;
    void *state, *transfer, *draw;
    std::uint64_t transfer_gen = 0; // guarded by m
    std::uint64_t draw_gen = 0;     // consumer-thread-only
    std::uint64_t next_gen = 1;     // sim-thread-only
    std::mutex m;                   // guards the state<->transfer pair
};

struct ExecutorImpl {
    void (*tick)(void *);
    void *user;
    enum class St { Paused, Playing, Step, Stopped };
    St st = St::Paused;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<std::uint64_t> delay_ns{0};
    std::atomic<std::uint64_t> ticks{0};
    std::thread worker;
};

} // namespace

Channel channel_create(std::size_t bytes) {
    if (!bytes) return {};
    auto *c = new ChannelImpl{bytes, ::operator new(bytes),
                              ::operator new(bytes), ::operator new(bytes)};
    std::memset(c->state, 0, bytes);
    std::memset(c->transfer, 0, bytes);
    std::memset(c->draw, 0, bytes);
    return {c};
}

void channel_destroy(Channel ch) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    if (!c) return;
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
    if (!c) return;
    std::lock_guard lk(c->m);
    std::swap(c->state, c->transfer);
    c->transfer_gen = c->next_gen++;
}

const void *channel_latest(Channel ch, std::uint64_t *gen) {
    auto *c = static_cast<ChannelImpl *>(ch.p);
    if (!c) return nullptr;
    {
        std::lock_guard lk(c->m);
        if (c->transfer_gen > c->draw_gen) {
            std::swap(c->transfer, c->draw);
            c->draw_gen = c->transfer_gen;
        }
    }
    if (gen) *gen = c->draw_gen;
    return c->draw_gen ? c->draw : nullptr;
}

Executor executor_create(void (*tick)(void *), void *user) {
    if (!tick) return {};
    auto *e = new ExecutorImpl{tick, user};
    e->worker = std::thread([e] {
        using St = ExecutorImpl::St;
        for (;;) {
            {
                std::unique_lock lk(e->m);
                e->cv.wait(lk, [e] { return e->st != St::Paused; });
                if (e->st == St::Stopped) return;
                if (e->st == St::Step) e->st = St::Paused; // one-shot
            }
            e->tick(e->user);
            e->ticks.fetch_add(1, std::memory_order_release);
            if (const auto d = e->delay_ns.load(std::memory_order_relaxed))
                std::this_thread::sleep_for(std::chrono::nanoseconds(d));
        }
    });
    return {e};
}

void executor_destroy(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e) return;
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
    if (!e) return;
    {
        std::lock_guard lk(e->m);
        if (e->st == ExecutorImpl::St::Stopped) return;
        e->st = st;
    }
    e->cv.notify_all();
}
} // namespace

void executor_play(Executor ex) { set_state(ex, ExecutorImpl::St::Playing); }
void executor_pause(Executor ex) { set_state(ex, ExecutorImpl::St::Paused); }
void executor_step(Executor ex) { set_state(ex, ExecutorImpl::St::Step); }

bool executor_playing(Executor ex) {
    auto *e = static_cast<ExecutorImpl *>(ex.p);
    if (!e) return false;
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

} // namespace seam
} // namespace sv
