/**
 * Base class for repeatability filters.
 */
#pragma once

#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

class RepeatabilityFilter {
   public:
    virtual ~RepeatabilityFilter() = default;
    virtual void apply(std::vector<ouster::LidarScan>& scans,
                       const sensor::sensor_info& info) = 0;
};

}  // namespace sensor_utils
}  // namespace ouster

