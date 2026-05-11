#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <limits>

namespace market {

struct LatencySnapshot {
    uint64_t sample_count{0};
    uint64_t avg_ns{0};
    uint64_t min_ns{0};
    uint64_t max_ns{0};
    uint64_t p50_ns{0};
    uint64_t p95_ns{0};
    uint64_t p99_ns{0};
    uint64_t p999_ns{0};

    std::array<uint64_t, 5> histogram{};
};

class LatencyStats {
public:

    explicit LatencyStats(size_t max_samples = 1'000'000)
        : max_samples_(max_samples) {

        samples_.reserve(std::min<size_t>(max_samples_, 16'384));
    }

    void record(uint64_t latency_ns) {

        total_latency_ns_ += latency_ns;
        total_samples_ += 1;
        min_ns_ = std::min(min_ns_, latency_ns);
        max_ns_ = std::max(max_ns_, latency_ns);

        if (samples_.size() < max_samples_) {
            samples_.push_back(latency_ns);
        } else {
            samples_[next_index_] = latency_ns;
        }
        next_index_ = (next_index_ + 1) % max_samples_;

        bucket_for(latency_ns);
    }

    LatencySnapshot snapshot() const {
        LatencySnapshot snap;
        const size_t recorded = std::min(total_samples_, max_samples_);
        if (recorded == 0) {
            return snap;
        }

        snap.sample_count = recorded;
        snap.avg_ns = total_latency_ns_ / total_samples_;
        snap.min_ns = min_ns_;
        snap.max_ns = max_ns_;
        snap.histogram = bucket_counts_;

        std::vector<uint64_t> sorted;
        sorted.reserve(recorded);

        const size_t available = std::min(samples_.size(), recorded);
        sorted.insert(sorted.end(), samples_.begin(), samples_.begin() + available);

        std::sort(sorted.begin(), sorted.end());

        auto percentile = [&](double q) {

            const size_t idx = std::min<size_t>(static_cast<size_t>(q * sorted.size()), sorted.size() - 1);
            return sorted[idx];
        };

        snap.p50_ns = percentile(0.50);
        snap.p95_ns = percentile(0.95);
        snap.p99_ns = percentile(0.99);
        snap.p999_ns = percentile(0.999);

        return snap;
    }

    void reset() {
        samples_.clear();
        next_index_ = 0;
        total_latency_ns_ = 0;
        total_samples_ = 0;
        min_ns_ = std::numeric_limits<uint64_t>::max();
        max_ns_ = 0;
        bucket_counts_.fill(0);
    }

    size_t max_samples() const {
        return max_samples_;
    }

private:
    size_t max_samples_;
    std::vector<uint64_t> samples_;
    size_t next_index_{0};
    uint64_t total_latency_ns_{0};
    uint64_t total_samples_{0};
    uint64_t min_ns_{std::numeric_limits<uint64_t>::max()};
    uint64_t max_ns_{0};
    std::array<uint64_t, 5> bucket_counts_{};

    const std::array<uint64_t, 4> bucket_bounds_{500, 1000, 2000, 5000};

    void bucket_for(uint64_t latency_ns) {
        size_t idx = 0;

        while (idx < bucket_bounds_.size() && latency_ns >= bucket_bounds_[idx]) {
            ++idx;
        }
        ++bucket_counts_[idx];
    }
};

}
