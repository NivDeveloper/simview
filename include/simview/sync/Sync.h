#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace sv {

struct Tick {
    std::uint64_t n = 0;
    double time = 0.0;
    double dt = 0.0;
};

namespace impl {

struct Channel {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Channel channel_create(std::size_t bytes);
void channel_destroy(Channel);
void *channel_state(Channel);
void channel_publish(Channel);
const void *channel_latest(Channel, std::uint64_t *gen);

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

template <typename T> class Channel {
  public:
    explicit Channel(std::size_t count)
        : c_(impl::channel_create(count * sizeof(T))), count_(count) {}

    ~Channel() { impl::channel_destroy(c_); }
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    std::span<T> State() {
        return {static_cast<T *>(impl::channel_state(c_)), count_};
    }

    void Publish() { impl::channel_publish(c_); }

    std::span<const T> Latest(std::uint64_t &gen) {
        const void *d = impl::channel_latest(c_, &gen);
        return d ? std::span<const T>{static_cast<const T *>(d), count_}
                 : std::span<const T>{};
    }

  private:
    impl::Channel c_;
    std::size_t count_;
};

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
