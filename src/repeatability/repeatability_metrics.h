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

struct MetricsOptions {
    size_t stride_rows = 4;
    size_t stride_cols = 8;
    size_t max_scans = 0;  // 0 = all
};

struct RepeatabilityMetrics {
    size_t frames_used = 0;
    size_t samples = 0;
    double mean_range_mm = 0.0;
    double std_range_mm = 0.0;
    double mean_abs_dev_mm = 0.0;
    double frame_mean_std_mm = 0.0;
    double valid_ratio = 0.0;
};

class RepeatabilityAnalyzer {
   public:
    static RepeatabilityMetrics compute_global_range_metrics(
        const std::vector<ouster::LidarScan>& scans,
        const sensor::sensor_info& info,
        const MetricsOptions& opts);
};

}  // namespace sensor_utils
}  // namespace ouster

