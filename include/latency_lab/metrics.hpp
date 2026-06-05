#pragma once

#include <vector>
#include <cstdint>
#include <chrono>
#include <iostream>

namespace latency_lab {


class LatencyRecorder {
public:
    LatencyRecorder() = default;
    void record(std::uint64_t latency_ns) noexcept {
        samples_.push_back(latency_ns);
    }

    std::uint64_t count() const noexcept {
        return samples_.size();
    }

    void print_summary() const;
    std::uint64_t min() const;
    std::uint64_t max() const;
    std::uint64_t p50() const;
    std::uint64_t p90() const;
    std::uint64_t p99() const;
    std::uint64_t p99_9() const;
    double mean() const;

    void clear() noexcept {
        samples_.clear();
    }

private:
    std::vector<std::uint64_t> samples_;
    static std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) noexcept;
    static std::string format_duration(std::uint64_t ns) noexcept;
};

class ScopedTimer {
public:
    explicit ScopedTimer(LatencyRecorder& recorder) : recorder_(recorder) {
        start_ = std::chrono::steady_clock::now();
    }

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        recorder_.record(static_cast<std::uint64_t>(duration));
    }

private:
    LatencyRecorder& recorder_;
    std::chrono::steady_clock::time_point start_;
};

}
