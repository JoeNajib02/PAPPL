/**
 * @file pcap_data_manipulator.h
 * @brief Public interface for loading, inspecting, and manipulating Ouster
 *        PCAP recordings in memory.
 *
 * The declarations here are intentionally light so downstream users can rely on
 * the class without including the heavier implementation. Each method is
 * documented with how it mutates internal state and the expectations on call
 * order (JSON before PCAP, validation helpers, etc.).
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>

#include <Eigen/Core>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"
#include "ouster/visibility.h"
#include "scan_statistics.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Simple 3D point representation used for cartesian exports.
 *
 * The coordinates are stored as double precision to match the SDK cartesian
 * output and default-construct to NaN so downstream consumers can quickly
 * detect uninitialized values.
 */
struct XYZPoint {
    double x_; ///< X coordinate in meters, NaN if uninitialized.
    double y_; ///< Y coordinate in meters, NaN if uninitialized.
    double z_; ///< Z coordinate in meters, NaN if uninitialized.
    XYZPoint()
        : x_(std::numeric_limits<double>::quiet_NaN()),
          y_(std::numeric_limits<double>::quiet_NaN()),
          z_(std::numeric_limits<double>::quiet_NaN()) {}
    XYZPoint(double x, double y, double z) : x_(x), y_(y), z_(z) {}
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
};

/**
 * @brief High-level helper to load PCAP/JSON pairs and manipulate decoded
 *        `LidarScan` frames.
 *
 * Wraps the Ouster SDK PCAP reader to produce a collection of scans, exposes
 * convenience filters, per-pixel editing, lightweight annotations, and basic
 * statistics used by the examples. All state is held in-memory. Intended usage:
 *  1) Call load_metadata(json) to parse sensor info. (Required precondition)
 *  2) Call load_pcap(pcap) to populate the scan collection.
 *  3) Run filtering/annotation/export helpers as needed.
 *
 * Notes and guarantees:
 *  - Thread-safety: instances are not thread-safe; guard externally.
 *  - Lifetime: all views/refs returned by getters become invalid after
 *    subsequent mutating calls (e.g., filter_scans, load_pcap).
 *  - Invalid ranges: zero values are treated as invalid/empty measurements.
 */
class OUSTER_API_CLASS PcapDataManipulator {
   public:
    /**
     * @brief Outcome reported by a scan-level processor.
     *
     * SUCCESS keeps the scan, FILTERED_OUT drops it, ERROR propagates an error
     * status to the caller (scan will be dropped).
     */
    enum class ProcessResult { SUCCESS, FILTERED_OUT, ERROR };

    /**
     * @brief Callback invoked for each scan when filtering.
     *
     * The callback receives the scan index, a mutable scan reference, and the
     * parsed sensor metadata. It should return a ProcessResult indicating
     * whether to keep, drop, or flag an error for the scan.
     */
    using ScanProcessor = std::function<ProcessResult(
        size_t, LidarScan&, const sensor::sensor_info&)>;

    /**
     * @brief Callback invoked for each pixel in `process_pixels`.
     *
     * Parameters (in order): scan index, row, column, mutable scan, sensor metadata.
     * Return `false` from the callback to stop iteration early. Iteration order
     * is scan-major, then row-major, then column-major.
     */
    using PixelProcessor = std::function<bool(size_t, size_t, size_t, LidarScan&, const sensor::sensor_info&)>;

    /**
     * @brief Basic statistics captured during ingestion of a PCAP file.
     *
     * Populated when load_pcap() completes; values remain valid until the next
     * call to load_pcap() or explicit reset.
     */
    struct DataStatistics {
        size_t total_scans = 0;          ///< Number of decoded scans retained.
        size_t total_packets = 0;        ///< Total packets consumed from the PCAP (lidar+imu).
        uint64_t start_timestamp = 0;    ///< Host timestamp (ns) of the first lidar packet.
        uint64_t end_timestamp = 0;      ///< Host timestamp (ns) of the last lidar packet.
        std::string pcap_path;           ///< Absolute or relative PCAP path used for loading.
        std::string json_path;           ///< Absolute or relative metadata JSON path.
        std::string sensor_serial;       ///< Serial number discovered in the metadata.
        std::string firmware_version;    ///< Firmware version discovered in the metadata.
    };

    /** @brief Construct an empty manipulator. */
    OUSTER_API_FUNCTION PcapDataManipulator();
    /** @brief Copy constructor producing a deep copy of the underlying scans. */
    OUSTER_API_FUNCTION PcapDataManipulator(const PcapDataManipulator& other);
    /** @brief Copy assignment producing a deep copy of the underlying scans. */
    OUSTER_API_FUNCTION PcapDataManipulator& operator=(const PcapDataManipulator& other);
    /** @brief Move constructor transferring ownership of loaded scans. */
    OUSTER_API_FUNCTION PcapDataManipulator(PcapDataManipulator&&) noexcept;
    /** @brief Move assignment transferring ownership of loaded scans. */
    OUSTER_API_FUNCTION PcapDataManipulator& operator=(PcapDataManipulator&&) noexcept;
    /** @brief Destroy the manipulator and release resources. */
    OUSTER_API_FUNCTION ~PcapDataManipulator();

    /**
     * @brief Load sensor metadata from an Ouster JSON file.
     * @param json_path Path to the metadata JSON produced by the sensor/SDK.
     * @return True if parsing succeeded.
     *
     * Populates sensor_info and statistics (serial, firmware) used by later
     * operations. Does not clear existing scans; call load_pcap() to refresh.
     */
    OUSTER_API_FUNCTION bool load_metadata(const std::string& json_path);
    /**
     * @brief Decode scans from a PCAP file using previously loaded metadata.
     * @param pcap_path Path to the PCAP file to ingest.
     * @return Number of scans decoded, or -1 on failure.
     *
     * Metadata must be loaded before calling this function. Existing scans and
     * annotations are cleared. Environment variable OUSTER_MAX_SCANS can cap
     * the number of scans ingested for quick runs.
     */
    OUSTER_API_FUNCTION int load_pcap(const std::string& pcap_path);

    /**
     * @brief Access the parsed sensor metadata.
     * @throws std::runtime_error if metadata has not been loaded.
     */
    OUSTER_API_FUNCTION const sensor::sensor_info& get_sensor_info() const;
    /** @brief Number of decoded scans currently held in memory. */
    OUSTER_API_FUNCTION size_t num_scans() const;
    /**
     * @brief Mutable access to a scan by index; throws on invalid index.
     * @warning References become invalid after any operation that resizes the
     *          internal scan vector (e.g., filter_scans, load_pcap).
     */
    OUSTER_API_FUNCTION LidarScan& get_scan(size_t scan_index);
    /** @brief Const access to a scan by index; throws on invalid index. */
    OUSTER_API_FUNCTION const LidarScan& get_scan(size_t scan_index) const;
    /**
     * @brief Convenience accessor returning references to all scans.
     * @note Returned references remain valid until the next mutating call.
     */
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<LidarScan>> get_all_scans();
    /** @brief Const convenience accessor returning references to all scans. */
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<const LidarScan>> get_all_scans() const;
    /** @brief Aggregate statistics captured while loading data. */
    OUSTER_API_FUNCTION const DataStatistics& get_statistics() const;
    /**
     * @brief Compute basic range statistics across scans using the helper in
     *        ScanStatistics.
     * @param stride_rows Sample every Nth row; defaults to 1 (no stride).
     * @param stride_cols Sample every Nth column; defaults to 1 (no stride).
     * @param max_scans Optional cap on number of scans to evaluate (0 = all).
     */
    OUSTER_API_FUNCTION FieldStatistics compute_range_statistics(
        size_t stride_rows = 1,
        size_t stride_cols = 1,
        size_t max_scans = 0) const;

    /**
     * @brief Apply a user callback to each scan and keep/drop based on result,
     *        updating annotations in parallel.
     * @return Number of scans retained after processing.
     *
     * The processor can mutate the scan; FILTERED_OUT removes the scan and its
     * annotations. ERROR is treated as filtered out but signals failure to the
     * caller via the return count comparison.
     */
    OUSTER_API_FUNCTION size_t filter_scans(const ScanProcessor& processor);

    /** @brief Zero out range measurements below the given threshold (mm). */
    OUSTER_API_FUNCTION size_t filter_by_range(uint32_t min_range_mm);
    /** @brief Zero out range where signal is below the given threshold (digital units). */
    OUSTER_API_FUNCTION size_t filter_by_signal(uint16_t min_signal);

    /**
     * @brief Read a pixel value from a named channel.
     * @param scan_index Scan to read from.
     * @param row Row index into the scan grid.
     * @param col Column index into the scan grid.
     * @param field Channel name (RANGE, SIGNAL, REFLECTIVITY).
     * @return The requested pixel value.
     * @throws std::invalid_argument if the field name is unsupported.
     */
    OUSTER_API_FUNCTION uint32_t get_pixel_value(size_t scan_index, size_t row, size_t col, const std::string& field) const;
    /**
     * @brief Write a pixel value into a named channel (no bounds clamping).
     * @return True if the value was written; false for invalid index/field.
     *
     * Does not validate value ranges; callers should clamp/validate as needed.
     */
    OUSTER_API_FUNCTION bool set_pixel_value(size_t scan_index, size_t row, size_t col, const std::string& field, uint32_t value);
    /**
     * @brief Iterate over every pixel across all scans.
     *
     * The callback is invoked with scan, row, and column indices. Returning
     * false from the callback stops iteration early. Use this for counting or
     * lightweight edits; for heavy work prefer direct field access.
     */
    OUSTER_API_FUNCTION void process_pixels(const PixelProcessor& processor);

    /**
     * @brief Attach a single annotation key/value to a scan.
     * @return False if the scan index is invalid.
     */
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, const std::string& key, const std::string& value);
    /** @brief Mutable access to a scan's annotations; throws on bad index. */
    OUSTER_API_FUNCTION std::map<std::string, std::string>& get_annotations(size_t scan_index);
    /** @brief Const access to a scan's annotations; throws on bad index. */
    OUSTER_API_FUNCTION const std::map<std::string, std::string>& get_annotations(size_t scan_index) const;
    /**
     * @brief Merge a set of annotations into a scan.
     * @return False if the scan index is invalid.
     */
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, const std::map<std::string, std::string>& annotations);
    /**
     * @brief Merge moveable annotations into a scan.
     * @return False if the scan index is invalid.
     */
    OUSTER_API_FUNCTION bool annotate_scan(size_t scan_index, std::map<std::string, std::string>&& annotations);
    /** @brief Retrieve all annotations for a scan; throws on bad index. */
    OUSTER_API_FUNCTION const std::map<std::string, std::string>& get_all_annotations(size_t scan_index) const;

    /** @brief Check whether a scan index is within bounds. */
    OUSTER_API_FUNCTION bool validate_scan_index(size_t scan_index) const;
    /** @brief True if metadata has been loaded successfully. */
    OUSTER_API_FUNCTION bool is_metadata_loaded() const;

    /**
     * @brief Return finite XYZ points computed from a scan's range channel.
     * @param scan_index Scan to convert to XYZ.
     * @return Vector of points with non-NaN coordinates.
     *
     * Uses `ouster::cartesian` with dual-return LUT enabled. Points with any
     * non-finite component are dropped.
     */
    OUSTER_API_FUNCTION std::vector<XYZPoint> get_valid_points(size_t scan_index) const;
    /**
     * @brief Convenience filter that zeros ranges below a threshold.
     * @return References to the filtered scans.
     * @deprecated Prefer filter_by_range(); kept for backwards compatibility.
     */
    OUSTER_API_FUNCTION std::vector<std::reference_wrapper<LidarScan>> filter_scans_by_range(uint32_t min_range_mm);

    /**
     * @brief Convert a scan's range channel into an XYZ point cloud matrix.
     * @param scan_index Scan to convert.
     * @return Eigen matrix shaped like the SDK cartesian output (float).
     *
     * The resulting matrix mirrors the SDK cartesian layout (flattened rows),
     * using float precision for convenience in examples.
     */
    OUSTER_API_FUNCTION Eigen::MatrixXf get_point_cloud(size_t scan_index) const;

    /**
     * @brief Export a scan to CSV including XYZ and core channels.
     * @param scan_index Scan to export.
     * @param output_path Destination CSV path.
     * @param include_headers Whether to write column headers.
     * @param include_invalid Whether to include zero/invalid ranges.
     * @return True if the file was written.
     *
     * XYZ values follow the SDK cartesian output ordering (flattened). Includes
     * RANGE, SIGNAL, and REFLECTIVITY channels.
     */
    OUSTER_API_FUNCTION bool export_scan_to_csv(size_t scan_index, const std::string& output_path, bool include_headers, bool include_invalid) const;
    /**
     * @brief Write raw PCAP bytes to disk.
     * @param output_path Destination PCAP path.
     * @param raw_data Raw PCAP buffer to write.
     * @return True if the file was written.
     */
    OUSTER_API_FUNCTION bool write_pcap(const std::string& output_path, const std::vector<uint8_t>& raw_data) const;

   private:
    class Impl; ///< PIMPL holding scans, metadata, and annotations.
    std::unique_ptr<Impl> pimpl_; ///< Opaque implementation storage.
};

}  // namespace sensor_utils
}  // namespace ouster
