/**
 * @file repeatability/filter_base.h
 * @brief Base interface for all repeatability filters used in the pipeline.
 *
 * Derivatives should be deterministic given identical inputs, avoid heap
 * allocations inside tight loops where possible, and assume that RANGE=0
 * denotes an invalid measurement. Filters should leave the shape of the scan
 * intact (no resizing) and avoid mutating metadata fields on the scan.
 */
#pragma once

#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

/** @brief Abstract base class for repeatability filters. */
class RepeatabilityFilter {
   public:
    virtual ~RepeatabilityFilter() = default;
    /**
     * @brief Apply the filter in-place to a collection of scans.
     * @param scans Mutable scans to operate on.
     * @param info Sensor metadata providing dimensions/calibration.
     *
     * Implementations may assume all scans share dimensions from `info`. Filters
     * are free to mutate range fields (and other channels if needed) but should
     * leave metadata intact. No ownership of `info` is taken. Callers expect
     * idempotent application over the provided scan vector (state should not be
     * preserved across apply() calls unless documented).
     */
    virtual void apply(std::vector<ouster::LidarScan>& scans,
                       const sensor::sensor_info& info) = 0;
};

}  // namespace sensor_utils
}  // namespace ouster
