/**
 * Implementation of PcapDataManipulator for loading and manipulating PCAP/JSON
 * data. This version reverts to the SDK packet iterator now that libpcap/Npcap
 * is available, adds dual-return profile by default, and logs packet/id errors.
 */

#include "pcap_data_manipulator.h"
#include "scan_filters.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <string>

#include "ouster/os_pcap.h"
#include "ouster/pcap_packet_source.h"
#include "ouster/packet.h"

namespace ouster {
namespace sensor_utils {

class PcapDataManipulator::Impl {
   public:
    std::vector<LidarScan> scans;
    nonstd::optional<sensor::sensor_info> sensor_info_opt;
    DataStatistics stats;
    std::vector<std::map<std::string, std::string>> scan_annotations;

    Impl() = default;
};

PcapDataManipulator::PcapDataManipulator() : pimpl_(new Impl()) {}

PcapDataManipulator::PcapDataManipulator(const PcapDataManipulator& other)
    : pimpl_(new Impl(*other.pimpl_)) {}

PcapDataManipulator& PcapDataManipulator::operator=(
    const PcapDataManipulator& other) {
    if (this != &other) {
        pimpl_ = std::make_unique<Impl>(*other.pimpl_);
    }
    return *this;
}

PcapDataManipulator::PcapDataManipulator(PcapDataManipulator&&) noexcept =
    default;

PcapDataManipulator& PcapDataManipulator::operator=(
    PcapDataManipulator&&) noexcept = default;

PcapDataManipulator::~PcapDataManipulator() = default;

bool PcapDataManipulator::load_metadata(const std::string& json_path) {
    try {
        std::cout << "[pcap_data_manipulator] Entering load_metadata, reading: " << json_path << std::endl;

        // Diagnostic: ensure file is readable and show snippet
        std::ifstream ifs(json_path);
        if (!ifs) {
            std::cout << "[pcap_data_manipulator] ERROR: cannot open metadata file: " << json_path << std::endl;
        } else {
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            std::cout << "[pcap_data_manipulator] Metadata file size: " << content.size() << " bytes" << std::endl;
            std::cout << "[pcap_data_manipulator] Metadata snippet: \n" << (content.size() > 200 ? content.substr(0,200) : content) << std::endl;
        }

        std::cout << "[pcap_data_manipulator] Calling sensor::metadata_from_json(...)" << std::endl;
        pimpl_->sensor_info_opt = sensor::metadata_from_json(json_path);
        std::cout << "[pcap_data_manipulator] Returned from sensor::metadata_from_json" << std::endl;

        pimpl_->stats.json_path = json_path;

        const auto& info = pimpl_->sensor_info_opt.value();
        pimpl_->stats.sensor_serial = info.sn;
        pimpl_->stats.firmware_version = info.image_rev;

        sensor::packet_format pf(info);
        std::cout << "[pcap_data_manipulator] Packet sizes (expected): lidar="
                  << pf.lidar_packet_size
                  << ", imu=" << pf.imu_packet_size << std::endl;
        std::cout << "[pcap_data_manipulator] Ports lidar="
                  << info.config.udp_port_lidar.value_or(0)
                  << ", imu=" << info.config.udp_port_imu.value_or(0)
                  << ", init_id=" << info.init_id << std::endl;

        std::cout << "[pcap_data_manipulator] Loaded metadata from: " << json_path << std::endl;
        std::cout << "[pcap_data_manipulator] Sensor SN=" << info.sn << ", prod_line=" << info.prod_line << ", resolution=" << info.format.columns_per_frame << "x" << info.format.pixels_per_column << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cout << "Error loading metadata: " << e.what() << std::endl;
        return false;
    }
}

int PcapDataManipulator::load_pcap(const std::string& pcap_path) {
    if (!is_metadata_loaded()) {
        throw std::runtime_error(
            "Metadata must be loaded before loading PCAP file");
    }

    try {
        std::cout << "[pcap_data_manipulator] Entering load_pcap" << std::endl;
        pimpl_->stats.pcap_path = pcap_path;
        pimpl_->scans.clear();
        pimpl_->scan_annotations.clear();

        std::cout << "[pcap_data_manipulator] load_pcap: cleared existing state" << std::endl;

        std::cout << "[pcap_data_manipulator] load_pcap: has_metadata="
                  << (pimpl_->sensor_info_opt.has_value() ? "true" : "false")
                  << std::endl;
        auto& info = pimpl_->sensor_info_opt.value();
        std::cout << "[pcap_data_manipulator] load_pcap: reference to sensor_info ready" << std::endl;
        size_t max_scans = 0;
        if (const char* env_max = std::getenv("OUSTER_MAX_SCANS")) {
            try {
                max_scans = std::stoull(env_max);
            } catch (...) {
                max_scans = 0;
            }
            if (max_scans > 0) {
                std::cout << "[pcap_data_manipulator] OUSTER_MAX_SCANS=" << max_scans
                          << ": will stop after this many scans" << std::endl;
            }
        }
        const auto lidar_packet_size = sensor::packet_format(info).lidar_packet_size;
        const auto imu_packet_size = sensor::packet_format(info).imu_packet_size;

        int detected_lidar_port = 0;
        int detected_imu_port = 0;

        // Quick sanity check to see what is inside the PCAP file and detect ports
        try {
            ouster::sensor_utils::PcapReader dbg_reader(pcap_path);
            int inspected = 0;
            while (inspected < 500 && dbg_reader.next_packet() != 0) {
                const auto pkt_info = dbg_reader.current_info();
                if (inspected == 0) {
                    std::cout << "[pcap_data_manipulator] First packet payload size="
                              << pkt_info.payload_size
                              << ", dst_port=" << pkt_info.dst_port << std::endl;
                }
                if (!detected_lidar_port &&
                    pkt_info.payload_size == static_cast<int>(lidar_packet_size)) {
                    detected_lidar_port = pkt_info.dst_port;
                }
                if (!detected_imu_port &&
                    pkt_info.payload_size == static_cast<int>(imu_packet_size)) {
                    detected_imu_port = pkt_info.dst_port;
                }
                if (detected_lidar_port && detected_imu_port) break;
                inspected++;
            }
        } catch (const std::exception& ex) {
            std::cout << "[pcap_data_manipulator] Warning: debug read failed: "
                      << ex.what() << std::endl;
        }

        if (detected_lidar_port) {
            info.config.udp_port_lidar = detected_lidar_port;
        }
        if (detected_imu_port) {
            info.config.udp_port_imu = detected_imu_port;
        }
        std::cout << "[pcap_data_manipulator] Using ports lidar="
                  << info.config.udp_port_lidar.value_or(0)
                  << ", imu=" << info.config.udp_port_imu.value_or(0)
                  << std::endl;
        pimpl_->sensor_info_opt = info;

        std::cout << "[pcap_data_manipulator] Opening PCAP: " << pcap_path << "\n";
        std::cout << "[pcap_data_manipulator] Beginning packet iteration" << std::endl;

        ouster::sensor_utils::PcapReader reader(pcap_path);
        auto pf_ptr = std::make_shared<sensor::packet_format>(info);
        ScanBatcher batcher(info);
        LidarScan current_scan(info);
        uint64_t first_timestamp = 0;
        uint64_t last_timestamp = 0;
        size_t total_packets = 0;

        bool stop = false;
        while (!stop && reader.next_packet() != 0) {
            const auto& pkt_info = reader.current_info();
            total_packets++;

            if (pkt_info.payload_size == static_cast<int>(pf_ptr->lidar_packet_size)) {
                sensor::LidarPacket lidar_packet(static_cast<int>(pf_ptr->lidar_packet_size));
                std::memcpy(lidar_packet.buf.data(), reader.current_data(),
                            pkt_info.payload_size);
                lidar_packet.host_timestamp =
                    static_cast<uint64_t>(pkt_info.timestamp.count()) * 1000;
                lidar_packet.format = pf_ptr;

                if (total_packets == 1) {
                    first_timestamp = lidar_packet.host_timestamp;
                }

                try {
                    if (batcher(lidar_packet, current_scan)) {
                        pimpl_->scans.push_back(current_scan);
                        pimpl_->scan_annotations.emplace_back();
                        last_timestamp = current_scan.timestamp()(0);
                        if (max_scans > 0 &&
                            pimpl_->scans.size() >= max_scans) {
                            std::cout << "[pcap_data_manipulator] Reached OUSTER_MAX_SCANS="
                                      << max_scans << ", stopping early." << std::endl;
                            stop = true;
                        }
                        current_scan = LidarScan(info);
                    }
                } catch (const std::exception& ex) {
                    std::cout << "[pcap_data_manipulator] Batcher error: "
                              << ex.what() << std::endl;
                }
            }
        }

        // Update statistics
        pimpl_->stats.total_scans = pimpl_->scans.size();
        pimpl_->stats.total_packets = total_packets;
        pimpl_->stats.start_timestamp = first_timestamp;
        pimpl_->stats.end_timestamp = last_timestamp;

        std::cout << "[pcap_data_manipulator] Finished packet iteration. Packets=" << total_packets << ", Scans=" << pimpl_->scans.size() << "\n";

        if (total_packets == 0 || pimpl_->stats.total_scans == 0) {
            std::cout << "[pcap_data_manipulator] No scans decoded. "
                      << "Packets seen: " << total_packets
                      << ". Check metadata vs PCAP (ports/profile/serial)."
                      << std::endl;
        }

        return static_cast<int>(pimpl_->scans.size());

    } catch (const std::exception& e) {
        std::cout << "Error loading PCAP: " << e.what() << std::endl;
        pimpl_->scans.clear();
        pimpl_->scan_annotations.clear();
        return -1;
    }
}

const sensor::sensor_info& PcapDataManipulator::get_sensor_info() const {
    if (!is_metadata_loaded()) {
        throw std::runtime_error("Metadata not loaded");
    }
    return pimpl_->sensor_info_opt.value();
}

size_t PcapDataManipulator::num_scans() const {
    return pimpl_->scans.size();
}

LidarScan& PcapDataManipulator::get_scan(size_t scan_index) {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    return pimpl_->scans[scan_index];
}

const LidarScan& PcapDataManipulator::get_scan(size_t scan_index) const {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    return pimpl_->scans[scan_index];
}

std::vector<std::reference_wrapper<LidarScan>>
PcapDataManipulator::get_all_scans() {
    std::vector<std::reference_wrapper<LidarScan>> result;
    for (auto& scan : pimpl_->scans) {
        result.push_back(std::reference_wrapper<LidarScan>(scan));
    }
    return result;
}

std::vector<std::reference_wrapper<const LidarScan>>
PcapDataManipulator::get_all_scans() const {
    std::vector<std::reference_wrapper<const LidarScan>> result;
    for (const auto& scan : pimpl_->scans) {
        result.push_back(
            std::reference_wrapper<const LidarScan>(scan));
    }
    return result;
}

const PcapDataManipulator::DataStatistics&
PcapDataManipulator::get_statistics() const {
    return pimpl_->stats;
}

FieldStatistics PcapDataManipulator::compute_range_statistics(
    size_t stride_rows, size_t stride_cols, size_t max_scans) const {
    if (!is_metadata_loaded()) {
        throw std::runtime_error("Metadata not loaded");
    }
    return ScanStatistics::range_statistics(
        pimpl_->scans, stride_rows, stride_cols, max_scans);
}

size_t PcapDataManipulator::filter_scans(const ScanProcessor& processor) {
    if (!is_metadata_loaded()) {
        throw std::runtime_error("Metadata not loaded");
    }

    ScanFilterEngine engine;
    const auto& info = pimpl_->sensor_info_opt.value();

    ScanFilterFn wrapped = [&](size_t idx, LidarScan& scan,
                               const sensor::sensor_info& info_ref) {
        const auto decision = processor(idx, scan, info_ref);
        switch (decision) {
            case ProcessResult::SUCCESS:
                return FilterDecision::Keep;
            case ProcessResult::FILTERED_OUT:
                return FilterDecision::FilteredOut;
            case ProcessResult::ERROR:
                return FilterDecision::Error;
        }
        return FilterDecision::Error;
    };

    return engine.apply(
        wrapped, info, pimpl_->scans, pimpl_->scan_annotations);
}

bool PcapDataManipulator::annotate_scan(
    size_t scan_index, const std::string& key, const std::string& value) {
    if (!validate_scan_index(scan_index)) {
        return false;
    }
    pimpl_->scan_annotations[scan_index][key] = value;
    return true;
}

std::map<std::string, std::string>& PcapDataManipulator::get_annotations(
    size_t scan_index) {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    return pimpl_->scan_annotations[scan_index];
}

const std::map<std::string, std::string>&
PcapDataManipulator::get_annotations(size_t scan_index) const {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    return pimpl_->scan_annotations[scan_index];
}

bool PcapDataManipulator::annotate_scan(size_t scan_index,
                                        const std::map<std::string, std::string>& annotations) {
    if (!validate_scan_index(scan_index)) {
        return false;
    }
    for (const auto& kv : annotations) {
        pimpl_->scan_annotations[scan_index][kv.first] = kv.second;
    }
    return true;
}

bool PcapDataManipulator::annotate_scan(size_t scan_index,
                                        std::map<std::string, std::string>&& annotations) {
    if (!validate_scan_index(scan_index)) {
        return false;
    }
    for (auto& kv : annotations) {
        pimpl_->scan_annotations[scan_index][kv.first] = std::move(kv.second);
    }
    return true;
}

const std::map<std::string, std::string>&
PcapDataManipulator::get_all_annotations(size_t scan_index) const {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    return pimpl_->scan_annotations[scan_index];
}

bool PcapDataManipulator::validate_scan_index(size_t scan_index) const {
    return scan_index < pimpl_->scans.size();
}

bool PcapDataManipulator::is_metadata_loaded() const {
    return pimpl_->sensor_info_opt.has_value();
}

std::vector<XYZPoint> PcapDataManipulator::get_valid_points(
    size_t scan_index) const {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }

    const auto& scan = pimpl_->scans[scan_index];
    const auto& info = pimpl_->sensor_info_opt.value();

    // Use SDK LUT helpers: build XYZ lookup and compute cartesian from range
    auto lut = ouster::make_xyz_lut(info, true);
    auto range = scan.field(sensor::ChanField::RANGE);
    auto xyz = ouster::cartesian(range, lut);

    std::vector<XYZPoint> points;
    points.reserve(xyz.rows());
    for (int i = 0; i < xyz.rows(); ++i) {
        XYZPoint point{xyz(i, 0), xyz(i, 1), xyz(i, 2)};
        if (std::isfinite(point.x()) && std::isfinite(point.y()) &&
            std::isfinite(point.z())) {
            points.push_back(point);
        }
    }

    return points;
}

size_t PcapDataManipulator::filter_by_range(uint32_t min_range_mm) {
    if (!is_metadata_loaded()) {
        throw std::runtime_error("Metadata not loaded");
    }
    ScanFilterEngine engine;
    return engine.zero_if_below_range(pimpl_->scans, min_range_mm);
}

size_t PcapDataManipulator::filter_by_signal(uint16_t min_signal) {
    if (!is_metadata_loaded()) {
        throw std::runtime_error("Metadata not loaded");
    }
    ScanFilterEngine engine;
    return engine.zero_if_below_signal(pimpl_->scans, min_signal);
}

uint32_t PcapDataManipulator::get_pixel_value(size_t scan_index,
                                              size_t row,
                                              size_t col,
                                              const std::string& field) const {
    if (!validate_scan_index(scan_index)) {
        throw std::out_of_range("Scan index out of range");
    }
    const auto& scan = pimpl_->scans[scan_index];

    if (field == sensor::ChanField::RANGE) {
        auto ff = scan.field<uint32_t>(sensor::ChanField::RANGE);
        return ff(static_cast<int>(row), static_cast<int>(col));
    }
    if (field == sensor::ChanField::SIGNAL) {
        auto ff = scan.field<uint16_t>(sensor::ChanField::SIGNAL);
        return ff(static_cast<int>(row), static_cast<int>(col));
    }
    if (field == sensor::ChanField::REFLECTIVITY) {
        auto ff = scan.field<uint16_t>(sensor::ChanField::REFLECTIVITY);
        return ff(static_cast<int>(row), static_cast<int>(col));
    }
    throw std::invalid_argument("Unsupported channel field");
}

bool PcapDataManipulator::set_pixel_value(size_t scan_index,
                                          size_t row,
                                          size_t col,
                                          const std::string& field,
                                          uint32_t value) {
    if (!validate_scan_index(scan_index)) {
        return false;
    }
    auto& scan = pimpl_->scans[scan_index];

    if (field == sensor::ChanField::RANGE) {
        auto ff = scan.field<uint32_t>(sensor::ChanField::RANGE);
        ff(static_cast<int>(row), static_cast<int>(col)) =
            static_cast<uint32_t>(value);
        return true;
    }
    if (field == sensor::ChanField::SIGNAL) {
        auto ff = scan.field<uint16_t>(sensor::ChanField::SIGNAL);
        ff(static_cast<int>(row), static_cast<int>(col)) =
            static_cast<uint16_t>(value);
        return true;
    }
    if (field == sensor::ChanField::REFLECTIVITY) {
        auto ff = scan.field<uint16_t>(sensor::ChanField::REFLECTIVITY);
        ff(static_cast<int>(row), static_cast<int>(col)) =
            static_cast<uint16_t>(value);
        return true;
    }
    return false;
}

void PcapDataManipulator::process_pixels(const PixelProcessor& processor) {
    if (!is_metadata_loaded()) return;
    const auto& info = pimpl_->sensor_info_opt.value();
    for (size_t s = 0; s < pimpl_->scans.size(); ++s) {
        auto& scan = pimpl_->scans[s];
        for (int r = 0; r < info.h(); ++r) {
            for (int c = 0; c < info.w(); ++c) {
                if (!processor(s, r, c, scan, info)) return;
            }
        }
    }
}

Eigen::MatrixXf PcapDataManipulator::get_point_cloud(size_t scan_index) const {
    if (!validate_scan_index(scan_index)) throw std::out_of_range("Scan index out of range");
    const auto& scan = pimpl_->scans[scan_index];
    const auto& info = pimpl_->sensor_info_opt.value();

    auto lut = ouster::make_xyz_lut(info, true);
    auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
    auto cloud = ouster::cartesian(range, lut);

    // cartesian returns (rows*cols) x 3 (or flattened depending on SDK). We'll return a float matrix with same dims
    Eigen::MatrixXf mat(cloud.rows(), cloud.cols());
    for (int r = 0; r < cloud.rows(); ++r) {
        for (int c = 0; c < cloud.cols(); ++c) {
            mat(r, c) = static_cast<float>(cloud(r, c));
        }
    }
    return mat;
}

bool PcapDataManipulator::export_scan_to_csv(size_t scan_index,
                                             const std::string& output_path,
                                             bool include_headers,
                                             bool include_invalid) const {
    if (!validate_scan_index(scan_index)) {
        return false;
    }
    const auto& scan = pimpl_->scans[scan_index];
    const auto& info = pimpl_->sensor_info_opt.value();

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        return false;
    }

    if (include_headers) {
        ofs << "x,y,z,range,signal,reflectivity\n";
    }

    // Build LUT and compute cartesian points
    auto lut = ouster::make_xyz_lut(info, true);
    auto xyz = ouster::cartesian(scan.field(sensor::ChanField::RANGE), lut);
    auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
    auto signal = scan.field<uint16_t>(sensor::ChanField::SIGNAL);
    auto refl = scan.field<uint16_t>(sensor::ChanField::REFLECTIVITY);

    for (int row = 0; row < xyz.rows(); ++row) {
        for (int col = 0; col < xyz.cols(); ++col) {
            if (!include_invalid && range(row, col) == 0) continue;
            ofs << xyz(row, col) << "," << xyz(row, col + xyz.rows()) << ","
                << xyz(row, col + 2 * xyz.rows()) << ","  // Flattened array
                << range(row, col) << "," << signal(row, col) << ","
                << refl(row, col) << "\n";
        }
    }
    return true;
}

bool PcapDataManipulator::write_pcap(const std::string& output_path,
                                     const std::vector<uint8_t>& raw_data) const {
    std::ofstream ofs(output_path, std::ios::binary);
    if (!ofs.is_open()) {
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
    return true;
}

}  // namespace sensor_utils
}  // namespace ouster
