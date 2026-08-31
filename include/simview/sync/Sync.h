#pragma once

#include "../Types.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <ranges>
#include <utility>

namespace sv {

struct Tick {
    std::uint64_t n = 0;
    double time = 0.0;
    double dt = 0.0;
};

namespace impl {

struct SyncGate {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

SyncGate sync_gate_create();
void sync_gate_destroy(SyncGate);
void sync_gate_retain(SyncGate);
void sync_gate_release(SyncGate);
void sync_gate_publish(SyncGate);
void sync_gate_flip(SyncGate);
int sync_gate_next(SyncGate);
int sync_gate_current(SyncGate);
int sync_gate_shown(SyncGate);
std::uint64_t sync_gate_generation(SyncGate);
void sync_gate_set_stamper(SyncGate, std::uint64_t (*)(void *), void *user);
std::uint64_t sync_gate_shown_stamp(SyncGate);

struct Executor {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Executor executor_create(void (*tick)(void *), void *user);
Executor executor_create_timed(void (*tick)(const Tick &, void *), void *user);
void executor_destroy(Executor);
void executor_play(Executor);
void executor_pause(Executor);
void executor_step(Executor);
void executor_advance(Executor, std::uint64_t n);
void executor_restart(Executor);
void executor_on_restart(Executor, void (*fn)(void *), void *user);
bool executor_restarted(Executor);
bool executor_playing(Executor);
void executor_set_delay_ns(Executor, std::uint64_t);
std::uint64_t executor_delay_ns(Executor);
void executor_set_dt(Executor, double);
Tick executor_tick(Executor);
double executor_rate(Executor);
std::uint64_t executor_ticks(Executor);

}

template <typename T> class Sync {
  public:
    Sync() : g_(impl::sync_gate_create()) {}

    explicit Sync(const T &init)
        requires std::copy_constructible<T>
        : Sync() {
        for (auto &s : s_)
            s = init;
    }

    ~Sync() { impl::sync_gate_destroy(g_); }
    Sync(const Sync &) = delete;
    Sync &operator=(const Sync &) = delete;

    T &Next() { return s_[impl::sync_gate_next(g_)]; }
    const T &Current() const { return s_[impl::sync_gate_current(g_)]; }
    void Publish() { impl::sync_gate_publish(g_); }

    void Publish(T &&v) {
        Next() = std::move(v);
        Publish();
    }

    const T &Shown() const { return s_[impl::sync_gate_shown(g_)]; }
    std::uint64_t Generation() const { return impl::sync_gate_generation(g_); }
    impl::SyncGate Gate() const { return g_; }

  private:
    T s_[3];
    impl::SyncGate g_;
};

template <typename T>
    requires std::ranges::contiguous_range<const T> &&
             std::ranges::sized_range<const T> &&
             std::same_as<std::ranges::range_value_t<const T>, float>
HostSource host_of(const Sync<T> &s) {
    return {+[](const void *u, std::size_t *bytes,
                std::uint64_t *gen) -> const void * {
                const auto &sync = *static_cast<const Sync<T> *>(u);
                *bytes = 0;
                *gen = sync.Generation();
                if (!*gen)
                    return nullptr;
                const T &r = sync.Shown();
                *bytes = std::ranges::size(r) * sizeof(float);
                return std::ranges::data(r);
            },
            &s};
}

class Executor {
  public:
    Executor() = default;

    explicit Executor(std::function<void()> tick)
        : tick_(new std::function<void()>(std::move(tick))),
          e_(impl::executor_create(
              [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
              tick_)) {}

    explicit Executor(std::function<void(const Tick &)> tick)
        : timed_(new std::function<void(const Tick &)>(std::move(tick))),
          e_(impl::executor_create_timed(
              [](const Tick &t, void *u) {
                  (*static_cast<std::function<void(const Tick &)> *>(u))(t);
              },
              timed_)) {}

    ~Executor() {
        if (e_)
            impl::executor_destroy(e_);
        delete tick_;
        delete timed_;
        delete restart_;
    }

    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;

    impl::Executor Raw() const { return e_; }

    void Play() { impl::executor_play(e_); }
    void Pause() { impl::executor_pause(e_); }
    void Toggle() { Playing() ? Pause() : Play(); }
    void Step() { impl::executor_step(e_); }
    void Advance(std::uint64_t n = 1) { impl::executor_advance(e_, n); }
    void Restart() { impl::executor_restart(e_); }

    void OnRestart(std::function<void()> fn) {
        delete restart_;
        restart_ = new std::function<void()>(std::move(fn));
        impl::executor_on_restart(
            e_, [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
            restart_);
    }

    bool Restarted() { return impl::executor_restarted(e_); }
    bool Playing() const { return impl::executor_playing(e_); }
    void SetDelayNs(std::uint64_t ns) { impl::executor_set_delay_ns(e_, ns); }
    std::uint64_t DelayNs() const { return impl::executor_delay_ns(e_); }
    void SetDt(double dt) { impl::executor_set_dt(e_, dt); }
    Tick Now() const { return impl::executor_tick(e_); }
    double Rate() const { return impl::executor_rate(e_); }
    std::uint64_t Ticks() const { return impl::executor_ticks(e_); }

  private:
    std::function<void()> *tick_ = nullptr;
    std::function<void(const Tick &)> *timed_ = nullptr;
    std::function<void()> *restart_ = nullptr;
    impl::Executor e_;
};

}
