/**
 * Helpers for writing repeatability reports to disk.
 */

#include "repeatability/report_writer.h"

#include <fstream>

namespace ouster {
namespace sensor_utils {

bool RepeatabilityReportWriter::write_csv(
    const std::string& path,
    const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;

    ofs << "label,frames_used,samples,mean_range_mm,std_range_mm,mean_abs_dev_mm,"
           "frame_mean_std_mm,valid_ratio\n";
    for (const auto& row : rows) {
        const auto& m = row.second;
        ofs << row.first << ","
            << m.frames_used << ","
            << m.samples << ","
            << m.mean_range_mm << ","
            << m.std_range_mm << ","
            << m.mean_abs_dev_mm << ","
            << m.frame_mean_std_mm << ","
            << m.valid_ratio << "\n";
    }
    return true;
}

bool RepeatabilityReportWriter::write_json(
    const std::string& path,
    const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;

    ofs << "[\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& label = rows[i].first;
        const auto& m = rows[i].second;
        ofs << "  {\n";
        ofs << "    \"label\": \"" << label << "\",\n";
        ofs << "    \"frames_used\": " << m.frames_used << ",\n";
        ofs << "    \"samples\": " << m.samples << ",\n";
        ofs << "    \"mean_range_mm\": " << m.mean_range_mm << ",\n";
        ofs << "    \"std_range_mm\": " << m.std_range_mm << ",\n";
        ofs << "    \"mean_abs_dev_mm\": " << m.mean_abs_dev_mm << ",\n";
        ofs << "    \"frame_mean_std_mm\": " << m.frame_mean_std_mm << ",\n";
        ofs << "    \"valid_ratio\": " << m.valid_ratio << "\n";
        ofs << "  }";
        if (i + 1 < rows.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    return true;
}

}  // namespace sensor_utils
}  // namespace ouster

