/**
 * Repeatability metric computation helpers.
 */

#include "repeatability/repeatability_metrics.h"

#include <algorithm>
#include <vector>

#include "scan_statistics.h"

namespace ouster {
namespace sensor_utils {

RepeatabilityMetrics RepeatabilityAnalyzer::compute_global_range_metrics(
    const std::vector<ouster::LidarScan>& scans,
    const sensor::sensor_info& info,
    const MetricsOptions& opts) {
    RepeatabilityMetrics metrics{};
    if (scans.empty()) return metrics;

    const size_t max_scans =
        opts.max_scans > 0 ? std::min(opts.max_scans, scans.size())
                           : scans.size();
    const size_t stride_r = std::max<size_t>(1, opts.stride_rows);
    const size_t stride_c = std::max<size_t>(1, opts.stride_cols);

    std::vector<double> samples;
    samples.reserve(max_scans * 128);
    std::vector<double> frame_means;
    frame_means.reserve(max_scans);

    size_t total_possible = 0;

    for (size_t s = 0; s < max_scans; ++s) {
        const auto& scan = scans[s];
        auto range_field = scan.field<uint32_t>(sensor::ChanField::RANGE);
        double frame_sum = 0.0;
        size_t frame_count = 0;

        for (int r = 0; r < range_field.rows(); r += static_cast<int>(stride_r)) {
            for (int c = 0; c < range_field.cols(); c += static_cast<int>(stride_c)) {
                total_possible++;
                uint32_t val = range_field(r, c);
                if (val == 0) continue;
                frame_sum += static_cast<double>(val);
                frame_count++;
                samples.push_back(static_cast<double>(val));
            }
        }

        if (frame_count > 0) {
            frame_means.push_back(frame_sum / static_cast<double>(frame_count));
        }
    }

    metrics.frames_used = max_scans;
    metrics.samples = samples.size();
    metrics.valid_ratio = total_possible > 0
                              ? static_cast<double>(metrics.samples) /
                                    static_cast<double>(total_possible)
                              : 0.0;

    const double mean = ScanStatistics::mean(samples);
    metrics.mean_range_mm = mean;
    metrics.std_range_mm = ScanStatistics::stddev(samples, mean);
    metrics.mean_abs_dev_mm =
        ScanStatistics::mean_abs_deviation(samples, mean);

    const double frame_mean = ScanStatistics::mean(frame_means);
    metrics.frame_mean_std_mm = ScanStatistics::stddev(frame_means, frame_mean);

    (void)info;  // Signature kept for future use.
    return metrics;
}

}  // namespace sensor_utils
}  // namespace ouster

