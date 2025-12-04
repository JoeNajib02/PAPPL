/**
 * Repeatability filter pipeline orchestrator implementation.
 */

#include "repeatability/pipeline.h"

#include <utility>

namespace ouster {
namespace sensor_utils {

RepeatabilityPipeline::RepeatabilityPipeline(sensor::sensor_info info)
    : info_(std::move(info)) {}

void RepeatabilityPipeline::add_filter(
    std::unique_ptr<RepeatabilityFilter> filter) {
    filters_.push_back(std::move(filter));
}

std::vector<ouster::LidarScan> RepeatabilityPipeline::run(
    const std::vector<ouster::LidarScan>& scans) {
    std::vector<ouster::LidarScan> result = scans;
    for (auto& filter : filters_) {
        filter->apply(result, info_);
    }
    return result;
}

}  // namespace sensor_utils
}  // namespace ouster

