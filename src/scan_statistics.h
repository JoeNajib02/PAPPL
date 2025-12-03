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

struct FieldStatistics {
    size_t samples = 0;
    double mean = 0.0;
    double stddev = 0.0;
};

class ScanStatistics {
   public:
    static double mean(const std::vector<double>& values);
    static double stddev(const std::vector<double>& values, double mean);
    static double mean_abs_deviation(const std::vector<double>& values,
                                     double center);

    static FieldStatistics range_statistics(const ouster::LidarScan& scan,
                                            size_t stride_rows = 1,
                                            size_t stride_cols = 1);

    static FieldStatistics range_statistics(
        const std::vector<ouster::LidarScan>& scans,
        size_t stride_rows = 1,
        size_t stride_cols = 1,
        size_t max_scans = 0);
};

}  // namespace sensor_utils
}  // namespace ouster
