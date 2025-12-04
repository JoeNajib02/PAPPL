/**
 * Repeatability filter pipeline orchestrator.
 */
#pragma once

#include <memory>
#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"
#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

class RepeatabilityPipeline {
   public:
    explicit RepeatabilityPipeline(sensor::sensor_info info);

    void add_filter(std::unique_ptr<RepeatabilityFilter> filter);
    std::vector<ouster::LidarScan> run(
        const std::vector<ouster::LidarScan>& scans);

   private:
    sensor::sensor_info info_;
    std::vector<std::unique_ptr<RepeatabilityFilter>> filters_;
};

}  // namespace sensor_utils
}  // namespace ouster

