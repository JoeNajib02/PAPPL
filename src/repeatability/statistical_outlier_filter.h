/**
 * Statistical outlier rejection filter.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

class StatisticalOutlierFilter : public RepeatabilityFilter {
   public:
    StatisticalOutlierFilter(double z_thresh, uint32_t min_range_mm);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double z_thresh_;
    uint32_t min_range_mm_;
};

}  // namespace sensor_utils
}  // namespace ouster

