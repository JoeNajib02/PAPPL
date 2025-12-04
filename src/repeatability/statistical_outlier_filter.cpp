/**
 * Statistical outlier rejection filter.
 */

#include "repeatability/statistical_outlier_filter.h"

#include <cmath>
#include <vector>

#include "scan_statistics.h"

namespace ouster {
namespace sensor_utils {

StatisticalOutlierFilter::StatisticalOutlierFilter(double z_thresh,
                                                   uint32_t min_range_mm)
    : z_thresh_(z_thresh), min_range_mm_(min_range_mm) {}

void StatisticalOutlierFilter::apply(std::vector<ouster::LidarScan>& scans,
                                     const sensor::sensor_info& info) {
    (void)info;
    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        std::vector<double> vals;
        vals.reserve(static_cast<size_t>(range.rows() * range.cols()));
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v > 0) vals.push_back(static_cast<double>(v));
            }
        }

        const double mean = ScanStatistics::mean(vals);
        const double stddev = ScanStatistics::stddev(vals, mean);
        const double z_limit = stddev > 0 ? z_thresh_ * stddev : 0.0;

        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v == 0) continue;
                if (v < min_range_mm_) {
                    range(r, c) = 0;
                    continue;
                }
                double z = stddev > 0 ? std::abs(static_cast<double>(v) - mean)
                                      : 0.0;
                if (z_limit > 0 && z > z_limit) {
                    range(r, c) = 0;
                }
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster

