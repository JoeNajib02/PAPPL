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

/**
 * @brief Simple orchestrator that applies a series of repeatability filters.
 *
 * Filters are executed in the order they are added. Each filter mutates the
 * working copy of scans in-place; the original input vector is preserved by
 * copying it prior to processing. The pipeline stores the sensor metadata so
 * filters receive a stable reference even if the caller discards theirs. The
 * pipeline itself holds no persistent scan state between runs.
 */
class RepeatabilityPipeline {
   public:
    /** @brief Construct a pipeline bound to the supplied sensor metadata. */
    explicit RepeatabilityPipeline(sensor::sensor_info info);

    /** @brief Append a filter to the execution chain. */
    void add_filter(std::unique_ptr<RepeatabilityFilter> filter);
    /**
     * @brief Run all configured filters over a copy of the input scans.
     * @param scans Input scans (left unmodified).
     * @return Filtered copy of the scans.
     */
    std::vector<ouster::LidarScan> run(
        const std::vector<ouster::LidarScan>& scans);

   private:
    sensor::sensor_info info_; ///< Sensor metadata copied at construction time.
    std::vector<std::unique_ptr<RepeatabilityFilter>> filters_; ///< Ordered list of filters to execute.
};

}  // namespace sensor_utils
}  // namespace ouster
