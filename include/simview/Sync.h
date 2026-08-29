#pragma once

// The sim/render sync layer — the design carried over from its
// predecessor, contract for contract: the Executor starts Paused;
// play/pause/step are safe from any thread; step is one-shot; the
// delay paces the sim (0 = uncapped); destruction stops and joins.
// A Channel is a triple-buffered handoff: the sim thread writes
// state(), publish() swaps state<->transfer and bumps the generation,
// the consumer's latest() swaps transfer<->draw when newer — pointer
// swaps, never copies.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace simview {

// ── seam ────────────────────────────────────────────────────────────
struct Channel {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Channel channel_create(std::size_t bytes); // device-free, pure host
void channel_destroy(Channel);
void *channel_state(Channel); // the sim side's write slab
void channel_publish(Channel);
// The freshest published slab and its generation; null until the
// first publish. Call from ONE consumer thread.
const void *channel_latest(Channel, std::uint64_t *gen);

struct Executor {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Executor executor_create(void (*tick)(void *), void *user);
void executor_destroy(Executor); // stop + join
void executor_play(Executor);
void executor_pause(Executor);
void executor_step(Executor); // one tick, then Paused
bool executor_playing(Executor);
void executor_set_delay_ns(Executor, std::uint64_t); // 0 = uncapped
std::uint64_t executor_ticks(Executor);

// ── sugar ───────────────────────────────────────────────────────────
template <typename T> class HostChannel {
  public:
    explicit HostChannel(std::size_t count)
        : c_(channel_create(count * sizeof(T))), count_(count) {}
    ~HostChannel() { channel_destroy(c_); }
    HostChannel(const HostChannel &) = delete;
    HostChannel &operator=(const HostChannel &) = delete;

    std::span<T> state() {
        return {static_cast<T *>(channel_state(c_)), count_};
    }
    void publish() { channel_publish(c_); }
    std::span<const T> latest(std::uint64_t &gen) {
        const void *d = channel_latest(c_, &gen);
        return d ? std::span<const T>{static_cast<const T *>(d), count_}
                 : std::span<const T>{};
    }

  private:
    Channel c_;
    std::size_t count_;
};

class ExecutorHandle {
  public:
    ExecutorHandle() = default;
    explicit ExecutorHandle(std::function<void()> tick)
        : tick_(new std::function<void()>(std::move(tick))),
          e_(executor_create(
              [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
              tick_)) {}
    ~ExecutorHandle() {
        if (e_) executor_destroy(e_); // joins before the tick dies
        delete tick_;
    }
    ExecutorHandle(const ExecutorHandle &) = delete;
    ExecutorHandle &operator=(const ExecutorHandle &) = delete;

    void play() { executor_play(e_); }
    void pause() { executor_pause(e_); }
    void step() { executor_step(e_); }
    bool playing() const { return executor_playing(e_); }
    void set_delay_ns(std::uint64_t ns) { executor_set_delay_ns(e_, ns); }
    std::uint64_t ticks() const { return executor_ticks(e_); }

  private:
    std::function<void()> *tick_ = nullptr;
    Executor e_;
};

} // namespace simview
