#include "syspulse/writer.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>

namespace syspulse {

Writer::Writer(const Config& config, std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer)
    : config_(config), ring_buffer_(std::move(ring_buffer)) {
    open_file();
}

Writer::~Writer() {
    stop();
    join();
    if (out_file_.is_open()) {
        out_file_.flush();
        out_file_.close();
    }
}

void Writer::open_file() {
    if (out_file_.is_open()) {
        out_file_.flush();
        out_file_.close();
    }

    std::string path = config_.out_path;
    if (rotation_index_ > 0) {
        path += "." + std::to_string(rotation_index_);
    }

    // Ensure parent directories exist
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    out_file_.open(path, std::ios::out | std::ios::binary | std::ios::app);
    current_file_bytes_ = 0;
    if (std::filesystem::exists(p)) {
        std::error_code ec;
        current_file_bytes_ = std::filesystem::file_size(p, ec);
    }

    csv_header_written_ = (current_file_bytes_ > 0);
}

void Writer::start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&Writer::run, this);
}

void Writer::stop() {
    running_.store(false, std::memory_order_release);
}

void Writer::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Writer::run() {
    Sample sample;
    while (running_.load(std::memory_order_relaxed)) {
        if (ring_buffer_->try_pop(sample)) {
            write_sample(sample);
            samples_written_.fetch_add(1, std::memory_order_relaxed);
            check_rotation();
        } else {
            // Buffer empty, yield or briefly sleep to prevent busy spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Drain all remaining elements on shutdown
    flush_all();
}

void Writer::flush_all() {
    Sample sample;
    while (ring_buffer_->try_pop(sample)) {
        write_sample(sample);
        samples_written_.fetch_add(1, std::memory_order_relaxed);
        check_rotation();
    }
    if (out_file_.is_open()) {
        out_file_.flush();
    }
}

void Writer::write_sample(const Sample& sample) {
    if (config_.format == OutputFormat::Ndjson) {
        write_ndjson(sample);
    } else {
        write_csv(sample);
    }
}

void Writer::write_ndjson(const Sample& sample) {
    nlohmann::json j = sample;
    std::string line = j.dump() + "\n";
    out_file_.write(line.data(), static_cast<std::streamsize>(line.size()));
    current_file_bytes_ += line.size();
}

void Writer::write_csv(const Sample& sample) {
    if (!csv_header_written_) {
        std::string header = "timestamp_ns,hostname,system_cpu_total_pct,system_mem_total_bytes,"
                             "system_mem_available_bytes,system_swap_used_bytes,context_switches,page_faults,"
                             "pid,ppid,name,cpu_pct,rss_bytes,virtual_bytes,thread_count,handle_or_fd_count,"
                             "io_read_bytes,io_write_bytes\n";
        out_file_.write(header.data(), static_cast<std::streamsize>(header.size()));
        current_file_bytes_ += header.size();
        csv_header_written_ = true;
    }

    std::string base = std::to_string(sample.timestamp_ns) + "," +
                       sample.hostname + "," +
                       std::to_string(sample.system.cpu_total_pct) + "," +
                       std::to_string(sample.system.mem_total_bytes) + "," +
                       std::to_string(sample.system.mem_available_bytes) + "," +
                       std::to_string(sample.system.swap_used_bytes) + "," +
                       std::to_string(sample.system.context_switches) + "," +
                       std::to_string(sample.system.page_faults) + ",";

    if (sample.processes.empty()) {
        std::string line = base + "0,0,system,0,0,0,0,0,0,0\n";
        out_file_.write(line.data(), static_cast<std::streamsize>(line.size()));
        current_file_bytes_ += line.size();
    } else {
        for (const auto& p : sample.processes) {
            std::string line = base +
                               std::to_string(p.pid) + "," +
                               std::to_string(p.ppid) + "," +
                               "\"" + p.name + "\"," +
                               std::to_string(p.cpu_pct) + "," +
                               std::to_string(p.rss_bytes) + "," +
                               std::to_string(p.virtual_bytes) + "," +
                               std::to_string(p.thread_count) + "," +
                               std::to_string(p.handle_or_fd_count) + "," +
                               std::to_string(p.io_read_bytes) + "," +
                               std::to_string(p.io_write_bytes) + "\n";
            out_file_.write(line.data(), static_cast<std::streamsize>(line.size()));
            current_file_bytes_ += line.size();
        }
    }
}

void Writer::check_rotation() {
    if (config_.max_file_size_bytes > 0 && current_file_bytes_ >= config_.max_file_size_bytes) {
        rotation_index_++;
        open_file();
    }
}

} // namespace syspulse
