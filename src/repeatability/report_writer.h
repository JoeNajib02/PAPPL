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

/**
 * @brief Serialize repeatability metrics to human-readable formats.
 *
 * Provides lightweight CSV/JSON dumps suitable for plotting or quick inspection.
 * Does not perform directory creation; callers should ensure the parent path
 * exists. All values are written verbatim (no unit conversion); callers should
 * pre-scale if alternate units are desired.
 */
class RepeatabilityReportWriter {
   public:
    /**
     * @brief Write metrics rows to a CSV file.
     * @param path Destination CSV path.
     * @param rows Pairs of label and metrics to write.
     * @return True if the file was opened and written.
     *
     * The header is always emitted. Fields are comma-separated without quoting.
     * Numeric values are written with default stream formatting; adjust upstream
     * if you need fixed precision.
     */
    static bool write_csv(
        const std::string& path,
        const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
    /**
     * @brief Write metrics rows to a JSON array file.
     * @param path Destination JSON path.
     * @param rows Pairs of label and metrics to write.
     * @return True if the file was opened and written.
     *
     * The output is a compact array of objects suitable for quick inspection or
     * downstream ingestion by lightweight scripts. Keys mirror the field names
     * in RepeatabilityMetrics; numeric formatting uses default ostream rules.
     */
    static bool write_json(
        const std::string& path,
        const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
};

}  // namespace sensor_utils
}  // namespace ouster
