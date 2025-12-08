/**
 * Compare one candidate PCAP against a reference PCAP at keyed target points.
 * Applies the default repeatability filters, prints summary stats, and writes
 * a CSV with per-point data for the candidate (raw + filtered) versus reference.
 *
 * Usage:
 *   repeatability_single <metadata.json> <targets.csv> <output.csv>
 *                        <pcap_ref> <pcap_candidate>
 *                        [--grid-res-m=0.02] [--max-scans=5]
 *
 * - targets.csv: rows of "id,x,y" or "x,y" (meters, sensor frame). Lines
 *   starting with '#' are ignored. If no id is provided, the row index is used.
 * - The first PCAP is treated as reference. Z deltas are computed against its
 *   raw averages; filtered values are still recorded.
 * - Grid resolution controls how (x,y) are snapped to tolerate tiny jitter.
 * - max-scans limits how many scans per PCAP are averaged (0 = all).
 */

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pcap_data_manipulator.h"
#include "repeatability_filters.h"

using namespace ouster;
using namespace ouster::sensor_utils;

namespace {

struct TargetPoint {
    std::string id;
    double x = 0.0;
    double y = 0.0;
    int64_t key = 0;
};

struct MeanResults {
    std::vector<double> mean;
    std::vector<size_t> counts;
    size_t frames_used = 0;
};

struct AcquisitionResult {
    std::string label;
    MeanResults raw;
    MeanResults filt;
};

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

int64_t pack_key(long long gx, long long gy) {
    return (static_cast<int64_t>(gx) << 32) ^ static_cast<uint32_t>(gy);
}

int64_t key_from_xy(double x, double y, double grid_res_m) {
    const long long gx = static_cast<long long>(std::llround(x / grid_res_m));
    const long long gy = static_cast<long long>(std::llround(y / grid_res_m));
    return pack_key(gx, gy);
}

std::string basename(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

std::vector<TargetPoint> load_targets(const std::string& path,
                                      double grid_res_m) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open targets file: " + path);
    }

    std::vector<TargetPoint> targets;
    std::string line;
    size_t line_no = 0;
    while (std::getline(ifs, line)) {
        line_no++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string item;
        while (std::getline(ss, item, ',')) {
            cols.push_back(trim(item));
        }
        if (cols.size() < 2) {
            std::cerr << "Skipping line " << line_no
                      << " (need at least x,y): " << line << "\n";
            continue;
        }

        TargetPoint t;
        size_t col_idx = 0;
        if (cols.size() == 2) {
            t.id = std::to_string(targets.size());
        } else {
            t.id = cols[col_idx++];
        }
        try {
            t.x = std::stod(cols[col_idx++]);
            t.y = std::stod(cols[col_idx++]);
        } catch (const std::exception&) {
            std::cerr << "Skipping line " << line_no << " (bad number): " << line
                      << "\n";
            continue;
        }
        t.key = key_from_xy(t.x, t.y, grid_res_m);
        targets.push_back(t);
    }
    return targets;
}

void accumulate_scan(const LidarScan& scan,
                     const ouster::XYZLut& lut,
                     double grid_res_m,
                     const std::unordered_map<int64_t, size_t>& key_to_idx,
                     std::vector<double>& sum_z,
                     std::vector<size_t>& counts) {
    auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
    auto points = ouster::cartesian(scan, lut);
    const size_t width = scan.w;

    for (Eigen::Index i = 0; i < points.rows(); ++i) {
        const size_t row = static_cast<size_t>(i) / width;
        const size_t col = static_cast<size_t>(i) % width;
        if (range(static_cast<int>(row), static_cast<int>(col)) == 0) continue;

        const double x = points(i, 0);
        const double y = points(i, 1);
        const double z = points(i, 2);
        const auto key = key_from_xy(x, y, grid_res_m);
        const auto it = key_to_idx.find(key);
        if (it != key_to_idx.end()) {
            sum_z[it->second] += z;
            counts[it->second] += 1;
        }
    }
}

MeanResults compute_means(const std::vector<LidarScan>& scans,
                          size_t frames_to_use,
                          const ouster::XYZLut& lut,
                          const std::unordered_map<int64_t, size_t>& key_to_idx,
                          double grid_res_m) {
    const size_t target_count = key_to_idx.size();
    MeanResults res;
    res.frames_used = frames_to_use;
    res.mean.assign(target_count, std::numeric_limits<double>::quiet_NaN());
    res.counts.assign(target_count, 0);

    std::vector<double> sum_z(target_count, 0.0);
    const size_t max_frames =
        frames_to_use == 0 ? scans.size() : std::min(frames_to_use, scans.size());

    for (size_t i = 0; i < max_frames; ++i) {
        accumulate_scan(scans[i], lut, grid_res_m, key_to_idx, sum_z,
                        res.counts);
    }
    res.frames_used = max_frames;
    for (size_t i = 0; i < target_count; ++i) {
        if (res.counts[i] > 0) {
            res.mean[i] = sum_z[i] / static_cast<double>(res.counts[i]);
        }
    }
    return res;
}

RepeatabilityPipeline make_default_pipeline(const sensor::sensor_info& info) {
    RepeatabilityPipeline pipeline(info);
    pipeline.add_filter(std::make_unique<StatisticalOutlierFilter>(2.5, 1500));
    pipeline.add_filter(std::make_unique<PlanaritySmoother>(1, 150, 0.6));
    pipeline.add_filter(std::make_unique<NormalGuidedSmoother>(12.0, 0.6));
    return pipeline;
}

AcquisitionResult evaluate_acquisition(
    const std::string& pcap_path,
    const std::string& json_path,
    const std::unordered_map<int64_t, size_t>& key_to_idx,
    double grid_res_m,
    size_t max_scans) {
    PcapDataManipulator manip;
    if (!manip.load_metadata(json_path)) {
        throw std::runtime_error("Failed to load metadata: " + json_path);
    }
    if (manip.load_pcap(pcap_path) <= 0) {
        throw std::runtime_error("Failed to load PCAP: " + pcap_path);
    }

    auto info = manip.get_sensor_info();
    const auto lut = ouster::make_xyz_lut(info, true);

    std::vector<LidarScan> scans;
    scans.reserve(manip.num_scans());
    for (const auto& ref : manip.get_all_scans()) scans.push_back(ref);

    const size_t frames_to_use =
        max_scans == 0 ? scans.size() : std::min(max_scans, scans.size());

    auto raw = compute_means(scans, frames_to_use, lut, key_to_idx, grid_res_m);

    auto pipeline = make_default_pipeline(info);
    auto filtered_scans = pipeline.run(scans);
    auto filt =
        compute_means(filtered_scans, frames_to_use, lut, key_to_idx, grid_res_m);

    AcquisitionResult result;
    result.label = basename(pcap_path);
    result.raw = std::move(raw);
    result.filt = std::move(filt);
    return result;
}

struct ErrorStats {
    double mean_abs_m = 0.0;
    double max_abs_m = 0.0;
    size_t count = 0;
};

ErrorStats compute_error_stats(const MeanResults& ref,
                               const MeanResults& candidate) {
    ErrorStats stats{};
    const size_t n = std::min(ref.mean.size(), candidate.mean.size());
    for (size_t i = 0; i < n; ++i) {
        if (ref.counts[i] == 0 || candidate.counts[i] == 0) continue;
        const double dz = candidate.mean[i] - ref.mean[i];
        const double abs_dz = std::abs(dz);
        stats.mean_abs_m += abs_dz;
        stats.max_abs_m = std::max(stats.max_abs_m, abs_dz);
        stats.count++;
    }
    if (stats.count > 0) {
        stats.mean_abs_m /= static_cast<double>(stats.count);
    }
    return stats;
}

bool write_csv(const std::string& out_path,
               const std::vector<TargetPoint>& targets,
               const std::vector<AcquisitionResult>& results,
               bool append_mode) {
    if (results.size() < 2) return false;  // need ref + candidate
    const auto& ref = results.front().raw;

    bool write_header = true;
    if (append_mode) {
        std::ifstream existing(out_path, std::ios::binary | std::ios::ate);
        if (existing && existing.tellg() > 0) write_header = false;
    }

    std::ofstream ofs(out_path, append_mode ? std::ios::app : std::ios::trunc);
    if (!ofs.is_open()) return false;

    if (write_header) {
        ofs << "acq_label,point_id,x_m,y_m,z_ref_m,z_raw_m,z_filt_m,dz_raw_m,"
               "dz_filt_m,missing_raw,missing_filt,raw_samples,filt_samples,"
               "frames_used\n";
    }
    ofs << std::fixed << std::setprecision(6);

    // Only write candidate rows (skip ref rows).
    for (size_t r = 1; r < results.size(); ++r) {
        const auto& res = results[r];
        for (size_t i = 0; i < targets.size(); ++i) {
            const bool ref_missing =
                i >= ref.counts.size() || ref.counts[i] == 0 ||
                std::isnan(ref.mean[i]);
            const bool raw_missing =
                i >= res.raw.counts.size() || res.raw.counts[i] == 0 ||
                std::isnan(res.raw.mean[i]);
            const bool filt_missing =
                i >= res.filt.counts.size() || res.filt.counts[i] == 0 ||
                std::isnan(res.filt.mean[i]);

            const double z_ref =
                ref_missing ? std::numeric_limits<double>::quiet_NaN()
                            : ref.mean[i];
            const double z_raw =
                raw_missing ? std::numeric_limits<double>::quiet_NaN()
                            : res.raw.mean[i];
            const double z_filt =
                filt_missing ? std::numeric_limits<double>::quiet_NaN()
                             : res.filt.mean[i];

            const double dz_raw =
                (ref_missing || raw_missing)
                    ? std::numeric_limits<double>::quiet_NaN()
                    : (z_raw - z_ref);
            const double dz_filt =
                (ref_missing || filt_missing)
                    ? std::numeric_limits<double>::quiet_NaN()
                    : (z_filt - z_ref);

            ofs << res.label << "," << targets[i].id << "," << targets[i].x << ","
                << targets[i].y << "," << z_ref << "," << z_raw << "," << z_filt
                << "," << dz_raw << "," << dz_filt << ","
                << (raw_missing ? 1 : 0) << ","
                << (filt_missing ? 1 : 0) << ","
                << (i < res.raw.counts.size() ? res.raw.counts[i] : 0) << ","
                << (i < res.filt.counts.size() ? res.filt.counts[i] : 0) << ","
                << res.raw.frames_used << "\n";
        }
    }
    return true;
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  repeatability_single <metadata.json> <targets.csv> <output.csv> "
           "<pcap_ref> <pcap_candidate> "
           "[--grid-res-m=0.02] [--max-scans=5]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 6) {
        print_usage();
        return EXIT_FAILURE;
    }

    const std::string json_path = argv[1];
    const std::string targets_path = argv[2];
    const std::string out_path = argv[3];

    double grid_res_m = 0.02;  // snap (x,y) to 2 cm grid
    size_t max_scans = 5;      // average over first N scans; 0 = all
    std::vector<std::string> pcaps;
    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--grid-res-m=", 0) == 0) {
            grid_res_m = std::stod(arg.substr(std::string("--grid-res-m=").size()));
        } else if (arg.rfind("--max-scans=", 0) == 0) {
            max_scans =
                static_cast<size_t>(std::stoul(arg.substr(std::string("--max-scans=").size())));
        } else {
            pcaps.push_back(arg);
        }
    }

    if (pcaps.size() < 2) {
        std::cerr << "Need at least two PCAPs: reference and one candidate.\n";
        print_usage();
        return EXIT_FAILURE;
    }
    if (grid_res_m <= 0.0) {
        std::cerr << "Grid resolution must be positive\n";
        return EXIT_FAILURE;
    }

    try {
        std::cout << "\n=== Repeatability (single candidate) ===\n";
        std::cout << "Metadata: " << json_path << "\n";
        std::cout << "Targets:  " << targets_path << "\n";
        std::cout << "Output:   " << out_path << " (append mode)\n";
        std::cout << "Grid:     " << grid_res_m << " m, Max scans: " << max_scans
                  << " (0 = all)\n\n";

        auto targets = load_targets(targets_path, grid_res_m);
        if (targets.empty()) {
            std::cerr << "No valid targets loaded from " << targets_path << "\n";
            return EXIT_FAILURE;
        }

        std::unordered_map<int64_t, size_t> key_to_idx;
        for (size_t i = 0; i < targets.size(); ++i) {
            key_to_idx[targets[i].key] = i;
        }

        std::vector<AcquisitionResult> results;
        results.reserve(pcaps.size());
        std::cout << "Reference: " << pcaps.front() << "\n";
        for (size_t idx = 0; idx < pcaps.size(); ++idx) {
            const auto& pcap = pcaps[idx];
            std::cout << "Processing: " << pcap << "\n";
            results.push_back(
                evaluate_acquisition(pcap, json_path, key_to_idx, grid_res_m,
                                     max_scans));
        }

        if (!write_csv(out_path, targets, results, /*append_mode=*/true)) {
            std::cerr << "Failed to write CSV: " << out_path << "\n";
            return EXIT_FAILURE;
        }

        const auto& ref = results.front();
        const auto& candidate = results.back();
        const auto raw_stats = compute_error_stats(ref.raw, candidate.raw);
        const auto filt_stats = compute_error_stats(ref.raw, candidate.filt);
        std::cout << "Reference: " << ref.label
                  << " (frames used: " << ref.raw.frames_used << ")\n";
        std::cout << "  " << candidate.label << " -> raw mean|max |dz| = "
                  << raw_stats.mean_abs_m * 1000.0 << " mm / "
                  << raw_stats.max_abs_m * 1000.0 << " mm"
                  << " (samples " << raw_stats.count << ")\n"
                  << "                  filt mean|max |dz| = "
                  << filt_stats.mean_abs_m * 1000.0 << " mm / "
                  << filt_stats.max_abs_m * 1000.0 << " mm"
                  << " (samples " << filt_stats.count << ")\n";

        std::cout << "Appended per-point CSV to " << out_path
                  << " (grid=" << grid_res_m << " m, frames="
                  << results.front().raw.frames_used << " per acquisition)\n";
        std::cout << "\n=== Repeatability completed successfully ===\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
