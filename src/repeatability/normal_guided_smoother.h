/**
 * Geometry-guided smoothing using local normals from XYZ.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

class NormalGuidedSmoother : public RepeatabilityFilter {
   public:
    NormalGuidedSmoother(double max_angle_deg, double blend);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double max_angle_rad_;
    double blend_;
};

}  // namespace sensor_utils
}  // namespace ouster

