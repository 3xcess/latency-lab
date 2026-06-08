#include "latency_lab/metrics.hpp"
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace latency_lab {

std::string LatencyRecorder::format_duration(std::uint64_t ns) noexcept {
    std::ostringstream oss;
    if (ns < 1000) {
        oss << ns << " ns";
    } else if (ns < 1000000) {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(ns) / 1000.0) << " us";
    } else {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(ns) / 1000000.0) << " ms";
    }
    return oss.str();
}

std::uint64_t LatencyRecorder::percentile(const std::vector<std::uint64_t>& sorted, double p) noexcept {
    if (sorted.empty()) return 0;
    // Decision: Use linear interpolation for percentile calculation.
    // This is a simple and common approach.
    double index = (p / 100.0) * (sorted.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));

    if (lower == upper) {
        return sorted[lower];
    }

    // Linear interpolation between lower and upper.
    double fraction = index - lower;
    return static_cast<std::uint64_t>(
        sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction);
}

std::uint64_t LatencyRecorder::min() const {
    if (samples_.empty()) return 0;
    // Create a sorted copy to find min (we don't modify the original here).
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return sorted.front();
}

std::uint64_t LatencyRecorder::max() const {
    if (samples_.empty()) return 0;
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return sorted.back();
}

double LatencyRecorder::mean() const {
    if (samples_.empty()) return 0.0;
    return static_cast<double>(std::accumulate(samples_.begin(), samples_.end(), 0ULL)) /
           static_cast<double>(samples_.size());
}

std::uint64_t LatencyRecorder::p50() const {
    if (samples_.empty()) return 0;
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return percentile(sorted, 50.0);
}

std::uint64_t LatencyRecorder::p90() const {
    if (samples_.empty()) return 0;
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return percentile(sorted, 90.0);
}

std::uint64_t LatencyRecorder::p99() const {
    if (samples_.empty()) return 0;
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return percentile(sorted, 99.0);
}

std::uint64_t LatencyRecorder::p99_9() const {
    if (samples_.empty()) return 0;
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return percentile(sorted, 99.9);
}

void LatencyRecorder::print_summary() const {
    if (samples_.empty()) {
        std::cout << "No samples recorded.\n";
        return;
    }

    // Sort once for all percentile calculations.
    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    std::cout << "\n=== Latency Statistics ===\n";
    std::cout << "Samples: " << samples_.size() << "\n";
    std::cout << "Min:     " << format_duration(percentile(sorted, 0.0)) << "\n";
    std::cout << "P50:     " << format_duration(percentile(sorted, 50.0)) << "\n";
    std::cout << "P90:     " << format_duration(percentile(sorted, 90.0)) << "\n";
    std::cout << "P99:     " << format_duration(percentile(sorted, 99.0)) << "\n";
    std::cout << "P99.9:   " << format_duration(percentile(sorted, 99.9)) << "\n";
    std::cout << "Max:     " << format_duration(percentile(sorted, 100.0)) << "\n";
    std::cout << "Mean:    " << format_duration(static_cast<std::uint64_t>(mean())) << "\n";
    std::cout << "========================\n\n";
}

}
