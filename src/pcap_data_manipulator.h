/**
 * Minimal header matching pcap_data_manipulator.cpp
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <cmath>

#include <Eigen/Core>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"
#include "scan_statistics.h"

#ifndef OUSTER_API_CLASS
#define OUSTER_API_CLASS
#endif
#ifndef OUSTER_API_FUNCTION
#define OUSTER_API_FUNCTION
#endif

namespace ouster {
namespace sensor_utils {

struct XYZPoint {
    double x_;
    double y_;
    double z_;
    XYZPoint() : x_(NAN), y_(NAN), z_(NAN) {}
    XYZPoint(double x, double y, double z) : x_(x), y_(y), z_(z) {}
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
};

class OUSTER_API_CLASS PcapDataManipulator {
   public:
    enum class ProcessResult { SUCCESS, FILTERED_OUT, ERROR };

    using ScanProcessor = std::function<ProcessResult(
        size_t, LidarScan&, const sensor::sensor_info&)>;

    using PixelProcessor = std::function<bool(size_t, size_t, size_t, LidarScan&, const sensor::sensor_info&)>;

    struct DataStatistics {
        size_t total_scans = 0;
        size_t total_packets = 0;
        uint64_t start_timestamp = 0;
        uint64_t end_timestamp = 0;
        std::string pcap_path;
        std::string json_path;
        std::string sensor_serial;
        std::string firmware_version;
    };

    // Construction
    OUSTER_API_FUNCTION PcapDataManipulator();
    OUSTER_API_FUNCTION PcapDataManipulator(const PcapDataManipulator& other);
    OUSTER_API_FUNCTION PcapDataManipulator& operator=(const PcapDataManipulator& other);
    OUSTER_API_FUNCTION PcapDataManipulator(PcapDataManipulator&&) noexcept;
    OUSTER_API_FUNCTION PcapDataManipulator& operator=(PcapDataManipulator&&) noexcept;
    OUSTER_API_FUNCTION ~PcapDataManipulator();

    // Loading
    OUSTER_API_FUNCTION bool load_metadata(const std::string& json_path);
    OUSTER_API_FUNCTION int load_pcap(const std::string& pcap_path);

    // Data access
    OUSTER_API_FUNCTION const sensor::sensor_info& get_sensor_info() const;
    OUSTER_API_FUNCTION size_t num_scans() const;
    OUSTER_API_FUNCTION LidarScan& get_scan(size_t scan_index);
    OUSTER_API_FUNCTION const LidarScan& get_scan(size_t scan_index) const;
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<LidarScan>> get_all_scans();
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<const LidarScan>> get_all_scans() const;
    OUSTER_API_FUNCTION const DataStatistics& get_statistics() const;
    OUSTER_API_FUNCTION FieldStatistics compute_range_statistics(
        size_t stride_rows = 1,
        size_t stride_cols = 1,
        size_t max_scans = 0) const;

    // Scan-level manipulation
    OUSTER_API_FUNCTION size_t filter_scans(const ScanProcessor& processor);

    // Convenience filtering APIs (used by examples)
    OUSTER_API_FUNCTION size_t filter_by_range(uint32_t min_range_mm);
    OUSTER_API_FUNCTION size_t filter_by_signal(uint16_t min_signal);

    // Pixel-level helpers
    OUSTER_API_FUNCTION uint32_t get_pixel_value(size_t scan_index, size_t row, size_t col, const std::string& field) const;
    OUSTER_API_FUNCTION bool set_pixel_value(size_t scan_index, size_t row, size_t col, const std::string& field, uint32_t value);
    OUSTER_API_FUNCTION void process_pixels(const PixelProcessor& processor);

    // Annotations
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, const std::string& key, const std::string& value);
    OUSTER_API_FUNCTION std::map<std::string, std::string>& get_annotations(size_t scan_index);
    OUSTER_API_FUNCTION const std::map<std::string, std::string>& get_annotations(size_t scan_index) const;
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, const std::map<std::string, std::string>& annotations);
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, std::map<std::string, std::string>&& annotations);
    OUSTER_API_FUNCTION const std::map<std::string, std::string>& get_all_annotations(size_t scan_index) const;

    // Helpers
    OUSTER_API_FUNCTION bool validate_scan_index(size_t scan_index) const;
    OUSTER_API_FUNCTION bool is_metadata_loaded() const;

    // Points and filtering
    OUSTER_API_FUNCTION std::vector<XYZPoint> get_valid_points(size_t scan_index) const;
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<LidarScan>> filter_scans_by_range(uint32_t min_range_mm);

    // Point cloud generation (convenience used by examples)
    OUSTER_API_FUNCTION Eigen::MatrixXf get_point_cloud(size_t scan_index) const;

    // Export
    OUSTER_API_FUNCTION bool export_scan_to_csv(size_t scan_index, const std::string& output_path, bool include_headers, bool include_invalid) const;
    OUSTER_API_FUNCTION bool write_pcap(const std::string& output_path, const std::vector<uint8_t>& raw_data) const;

   private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace sensor_utils
}  // namespace ouster
