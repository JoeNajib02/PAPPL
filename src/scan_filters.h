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

enum class FilterDecision { Keep, FilteredOut, Error };

using ScanFilterFn = std::function<FilterDecision(
    size_t, ouster::LidarScan&, const sensor::sensor_info&)>;

class ScanFilterEngine {
   public:
    size_t apply(const ScanFilterFn& processor,
                 const sensor::sensor_info& info,
                 std::vector<ouster::LidarScan>& scans,
                 std::vector<std::map<std::string, std::string>>& annotations) const;

    size_t zero_if_below_range(std::vector<ouster::LidarScan>& scans,
                               uint32_t min_range_mm) const;

    size_t zero_if_below_signal(std::vector<ouster::LidarScan>& scans,
                                uint16_t min_signal) const;
};

}  // namespace sensor_utils
}  // namespace ouster
