/**
 * Shared statistics helpers for lidar scan data.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

/** @brief Summary statistics for a scalar channel. */
struct FieldStatistics {
    size_t samples = 0;  ///< Number of valid samples considered.
    double mean = 0.0;   ///< Arithmetic mean of the samples (same units as input).
    double stddev = 0.0; ///< Sample standard deviation (same units as input).
};

/**
 * @brief Small utility for computing summary stats over lidar ranges.
 *
 * All helpers are static; no state is stored. Methods ignore zero-valued range
 * measurements (treated as invalid/masked). Complexity is O(N) over the sampled
 * points; stride parameters can be used to downsample large scans.
 */
class ScanStatistics {
   public:
    /** @brief Compute the arithmetic mean of a vector; returns 0 for empty. */
    static double mean(const std::vector<double>& values);
    /** @brief Compute sample standard deviation around a provided mean. */
    static double stddev(const std::vector<double>& values, double mean);
    /** @brief Compute mean absolute deviation around a provided center. */
    static double mean_abs_deviation(const std::vector<double>& values,
                                     double center);

    /**
     * @brief Compute statistics for the range channel in a single scan.
     * @param scan The scan to sample.
     * @param stride_rows Subsample every Nth row (minimum 1).
     * @param stride_cols Subsample every Nth column (minimum 1).
     *
     * Skips zero values. Useful for quick health checks on a single frame.
     */
    static FieldStatistics range_statistics(const ouster::LidarScan& scan,
                                            size_t stride_rows = 1,
                                            size_t stride_cols = 1);

    /**
     * @brief Compute statistics for the range channel across multiple scans.
     * @param scans Collection of scans to sample.
     * @param stride_rows Subsample every Nth row (minimum 1).
     * @param stride_cols Subsample every Nth column (minimum 1).
     * @param max_scans Optional limit on how many scans to traverse (0 = all).
     *
     * Iterates sequentially; zero values are ignored. Use max_scans to bound
     * runtime on large datasets.
     */
    static FieldStatistics range_statistics(
        const std::vector<ouster::LidarScan>& scans,
        size_t stride_rows = 1,
        size_t stride_cols = 1,
        size_t max_scans = 0);
};

}  // namespace sensor_utils
}  // namespace ouster
