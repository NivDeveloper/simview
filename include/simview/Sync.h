#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace sv {

namespace seam {

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
void executor_destroy(Executor);
void executor_play(Executor);
void executor_pause(Executor);
void executor_step(Executor);
bool executor_playing(Executor);
void executor_set_delay_ns(Executor, std::uint64_t);
std::uint64_t executor_ticks(Executor);

}

template <typename T> class Channel {
  public:
    explicit Channel(std::size_t count)
        : c_(seam::channel_create(count * sizeof(T))), count_(count) {}
    ~Channel() { seam::channel_destroy(c_); }
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    std::span<T> State() {
        return {static_cast<T *>(seam::channel_state(c_)), count_};
    }
    void Publish() { seam::channel_publish(c_); }
    std::span<const T> Latest(std::uint64_t &gen) {
        const void *d = seam::channel_latest(c_, &gen);
        return d ? std::span<const T>{static_cast<const T *>(d), count_}
                 : std::span<const T>{};
    }

  private:
    seam::Channel c_;
    std::size_t count_;
};

class Executor {
  public:
    Executor() = default;
    explicit Executor(std::function<void()> tick)
        : tick_(new std::function<void()>(std::move(tick))),
          e_(seam::executor_create(
              [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
              tick_)) {}
    ~Executor() {
        if (e_) seam::executor_destroy(e_);
        delete tick_;
    }
    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;

    void Play() { seam::executor_play(e_); }
    void Pause() { seam::executor_pause(e_); }
    void Step() { seam::executor_step(e_); }
    bool Playing() const { return seam::executor_playing(e_); }
    void SetDelayNs(std::uint64_t ns) { seam::executor_set_delay_ns(e_, ns); }
    std::uint64_t Ticks() const { return seam::executor_ticks(e_); }

  private:
    std::function<void()> *tick_ = nullptr;
    seam::Executor e_;
};

}
