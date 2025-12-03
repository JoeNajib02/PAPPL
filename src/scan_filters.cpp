/**
 * Reusable filtering helpers for lidar scan collections.
 */

#include "scan_filters.h"

#include <utility>

namespace ouster {
namespace sensor_utils {

size_t ScanFilterEngine::apply(
    const ScanFilterFn& processor,
    const sensor::sensor_info& info,
    std::vector<ouster::LidarScan>& scans,
    std::vector<std::map<std::string, std::string>>& annotations) const {
    std::vector<ouster::LidarScan> kept_scans;
    std::vector<std::map<std::string, std::string>> kept_annotations;
    kept_scans.reserve(scans.size());
    kept_annotations.reserve(annotations.size());

    for (size_t i = 0; i < scans.size(); ++i) {
        auto decision = processor(i, scans[i], info);
        if (decision != FilterDecision::FilteredOut) {
            kept_scans.push_back(scans[i]);
            if (i < annotations.size()) {
                kept_annotations.push_back(std::move(annotations[i]));
            } else {
                kept_annotations.emplace_back();
            }
        }
    }

    scans.swap(kept_scans);
    annotations.swap(kept_annotations);
    return scans.size();
}

size_t ScanFilterEngine::zero_if_below_range(
    std::vector<ouster::LidarScan>& scans, uint32_t min_range_mm) const {
    size_t removed = 0;
    for (auto& scan : scans) {
        auto range_field = scan.field<uint32_t>(sensor::ChanField::RANGE);
        for (int r = 0; r < range_field.rows(); ++r) {
            for (int c = 0; c < range_field.cols(); ++c) {
                if (range_field(r, c) < min_range_mm) {
                    range_field(r, c) = 0;
                    removed++;
                }
            }
        }
    }
    return removed;
}

size_t ScanFilterEngine::zero_if_below_signal(
    std::vector<ouster::LidarScan>& scans, uint16_t min_signal) const {
    size_t removed = 0;
    for (auto& scan : scans) {
        auto signal_field = scan.field<uint16_t>(sensor::ChanField::SIGNAL);
        auto range_field = scan.field<uint32_t>(sensor::ChanField::RANGE);
        for (int r = 0; r < signal_field.rows(); ++r) {
            for (int c = 0; c < signal_field.cols(); ++c) {
                if (signal_field(r, c) < min_signal) {
                    range_field(r, c) = 0;
                    removed++;
                }
            }
        }
    }
    return removed;
}

}  // namespace sensor_utils
}  // namespace ouster
