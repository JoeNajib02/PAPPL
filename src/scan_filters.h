/**
 * Reusable filtering helpers for lidar scan collections.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Decision returned by a scan filter callback.
 *
 * Keep        - preserve the scan in the output list.
 * FilteredOut - drop the scan from the output list.
 * Error       - treat as a failure; scan is dropped.
 */
enum class FilterDecision { Keep, FilteredOut, Error };

/** @brief Callback signature for per-scan filtering. */
using ScanFilterFn = std::function<FilterDecision(
    size_t, ouster::LidarScan&, const sensor::sensor_info&)>;

/**
 * @brief Engine that applies scan-level filters and updates annotations.
 *
 * The engine owns no state; it simply mutates the provided scan and annotation
 * vectors in-place, keeping them in sync as scans are removed or modified.
 * Removal preserves order of kept scans. Annotations are dropped along with
 * their corresponding scans.
 */
class ScanFilterEngine {
   public:
    /**
     * @brief Apply a filter callback to each scan and drop filtered ones.
     * @param processor User-supplied decision function.
     * @param info Parsed sensor metadata for context.
     * @param scans Mutable collection of scans to filter in-place.
     * @param annotations Parallel annotation collection maintained alongside scans.
     * @return Number of scans kept after filtering.
     */
    size_t apply(const ScanFilterFn& processor,
                 const sensor::sensor_info& info,
                 std::vector<ouster::LidarScan>& scans,
                 std::vector<std::map<std::string, std::string>>& annotations) const;

    /** @brief Zero out range values that fall below a threshold. */
    size_t zero_if_below_range(std::vector<ouster::LidarScan>& scans,
                               uint32_t min_range_mm) const;

    /** @brief Zero out range values where the signal is below a threshold. */
    size_t zero_if_below_signal(std::vector<ouster::LidarScan>& scans,
                                uint16_t min_signal) const;
};

}  // namespace sensor_utils
}  // namespace ouster
