#pragma once

#include "config.hpp"
#include "model.hpp"
#include "spsc_ring_buffer.hpp"
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace syspulse {

class Writer {
public:
    Writer(const Config& config, std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer);
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    void start();
    void stop();
    void join();
    void flush_all();

    [[nodiscard]] size_t samples_written() const noexcept { return samples_written_.load(); }

private:
    void run();
    void write_sample(const Sample& sample);
    void write_ndjson(const Sample& sample);
    void write_csv(const Sample& sample);
    void check_rotation();
    void open_file();

    Config config_;
    std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer_;

    std::ofstream out_file_;
    uint64_t current_file_bytes_{0};
    uint32_t rotation_index_{0};

    std::atomic<bool> running_{false};
    std::atomic<size_t> samples_written_{0};
    std::thread thread_{};
    bool csv_header_written_{false};
};

} // namespace syspulse
