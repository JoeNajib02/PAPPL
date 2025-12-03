/**
 * Shared statistics helpers for lidar scan data.
 */

#include "scan_statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ouster {
namespace sensor_utils {

double ScanStatistics::mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double ScanStatistics::stddev(const std::vector<double>& values,
                              double mean_value) {
    if (values.size() < 2) return 0.0;
    double accum = 0.0;
    for (double v : values) {
        const double d = v - mean_value;
        accum += d * d;
    }
    return std::sqrt(accum / static_cast<double>(values.size() - 1));
}

double ScanStatistics::mean_abs_deviation(const std::vector<double>& values,
                                          double center) {
    if (values.empty()) return 0.0;
    double accum = 0.0;
    for (double v : values) {
        accum += std::abs(v - center);
    }
    return accum / static_cast<double>(values.size());
}

FieldStatistics ScanStatistics::range_statistics(const ouster::LidarScan& scan,
                                                 size_t stride_rows,
                                                 size_t stride_cols) {
    FieldStatistics stats{};
    auto range_field = scan.field<uint32_t>(sensor::ChanField::RANGE);

    const int row_step = static_cast<int>(std::max<size_t>(stride_rows, 1));
    const int col_step = static_cast<int>(std::max<size_t>(stride_cols, 1));

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(range_field.rows() * range_field.cols()));

    for (int r = 0; r < range_field.rows(); r += row_step) {
        for (int c = 0; c < range_field.cols(); c += col_step) {
            const uint32_t v = range_field(r, c);
            if (v == 0) continue;
            samples.push_back(static_cast<double>(v));
        }
    }

    stats.samples = samples.size();
    stats.mean = mean(samples);
    stats.stddev = stddev(samples, stats.mean);
    return stats;
}

FieldStatistics ScanStatistics::range_statistics(
    const std::vector<ouster::LidarScan>& scans,
    size_t stride_rows,
    size_t stride_cols,
    size_t max_scans) {
    FieldStatistics stats{};
    if (scans.empty()) return stats;

    const size_t limit =
        max_scans > 0 ? std::min(max_scans, scans.size()) : scans.size();

    std::vector<double> samples;
    samples.reserve(limit * static_cast<size_t>(scans.front().h * scans.front().w));

    const int row_step = static_cast<int>(std::max<size_t>(stride_rows, 1));
    const int col_step = static_cast<int>(std::max<size_t>(stride_cols, 1));

    for (size_t s = 0; s < limit; ++s) {
        auto range_field = scans[s].field<uint32_t>(sensor::ChanField::RANGE);
        for (int r = 0; r < range_field.rows(); r += row_step) {
            for (int c = 0; c < range_field.cols(); c += col_step) {
                const uint32_t v = range_field(r, c);
                if (v == 0) continue;
                samples.push_back(static_cast<double>(v));
            }
        }
    }

    stats.samples = samples.size();
    stats.mean = mean(samples);
    stats.stddev = stddev(samples, stats.mean);
    return stats;
}

}  // namespace sensor_utils
}  // namespace ouster
