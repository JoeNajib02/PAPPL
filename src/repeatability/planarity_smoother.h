/**
 * Local planarity-aware smoothing.
 */
#pragma once

#include <cstdint>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

class PlanaritySmoother : public RepeatabilityFilter {
   public:
    PlanaritySmoother(int radius, uint32_t base_range_mm, double plane_threshold);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int radius_;
    uint32_t base_range_mm_;
    double plane_threshold_;
};

}  // namespace sensor_utils
}  // namespace ouster

