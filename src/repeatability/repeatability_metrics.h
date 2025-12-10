/**
 * Repeatability metric structures and analyzer.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

/** @brief Controls sampling density for metric computation. */
struct MetricsOptions {
    size_t stride_rows = 4; ///< Sample every Nth row (min 1).
    size_t stride_cols = 8; ///< Sample every Nth column (min 1).
    size_t max_scans = 0;   ///< Optional cap on frames to process (0 = all).
};

/** @brief Aggregate repeatability metrics derived from range data. */
struct RepeatabilityMetrics {
    size_t frames_used = 0;       ///< Frames included in the computation (respecting max_scans).
    size_t samples = 0;           ///< Number of valid range samples used.
    double mean_range_mm = 0.0;   ///< Mean range across all samples (millimeters).
    double std_range_mm = 0.0;    ///< Sample stddev of range across all samples (millimeters).
    double mean_abs_dev_mm = 0.0; ///< Mean absolute deviation from the global mean (millimeters).
    double frame_mean_std_mm = 0.0; ///< Stddev of per-frame mean ranges (millimeters).
    double valid_ratio = 0.0;     ///< Ratio of valid samples to total possible (after strides).
};

/** @brief Computes repeatability metrics over collections of scans. */
class RepeatabilityAnalyzer {
   public:
    /**
     * @brief Compute global range metrics across a set of scans.
     * @param scans Input scans to evaluate.
     * @param info Sensor metadata (reserved for future use).
     * @param opts Sampling and frame-limit options.
     *
     * Ignores zero ranges. Frames are processed in order until `max_scans`
     * (0 = all). Strides downsample the grid to reduce cost while maintaining
     * spatial coverage. Uses `ScanStatistics` helpers for core calculations.
     */
    static RepeatabilityMetrics compute_global_range_metrics(
        const std::vector<ouster::LidarScan>& scans,
        const sensor::sensor_info& info,
        const MetricsOptions& opts);
};

}  // namespace sensor_utils
}  // namespace ouster
