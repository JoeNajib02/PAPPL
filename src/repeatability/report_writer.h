/**
 * Helpers for writing repeatability reports.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "repeatability/repeatability_metrics.h"

namespace ouster {
namespace sensor_utils {

class RepeatabilityReportWriter {
   public:
    static bool write_csv(
        const std::string& path,
        const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
    static bool write_json(
        const std::string& path,
        const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
};

}  // namespace sensor_utils
}  // namespace ouster

