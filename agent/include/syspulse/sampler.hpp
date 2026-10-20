#pragma once

#include "arena_allocator.hpp"
#include "collector.hpp"
#include "config.hpp"
#include "model.hpp"
#include "spsc_ring_buffer.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace syspulse {

class Sampler {
public:
    Sampler(const Config& config,
            std::shared_ptr<ICollector> collector,
            std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    void start();
    void stop();
    void join();

    [[nodiscard]] size_t samples_collected() const noexcept { return samples_collected_.load(); }
    [[nodiscard]] size_t samples_dropped() const noexcept { return samples_dropped_.load(); }

private:
    void run();
    void collect_cycle();
    AgentSelfStats measure_self_overhead();

    Config config_;
    std::shared_ptr<ICollector> collector_;
    std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer_;
    ArenaAllocator scratch_arena_;
    std::string hostname_;

    std::atomic<bool> running_{false};
    std::atomic<size_t> samples_collected_{0};
    std::atomic<size_t> samples_dropped_{0};
    std::thread thread_{};

    // Tracking for self CPU%
    uint64_t prev_self_cpu_time_{0};
    uint64_t prev_self_sample_ts_{0};
};

} // namespace syspulse
