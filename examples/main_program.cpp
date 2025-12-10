/**
 * @file main_program.cpp
 * @brief MainProgram: compare a reference PCAP to another PCAP on 10 target
 *        points, before/after each repeatability filter. Outputs a CSV with
 *        per-filter values.
 *
 * Usage:
 *   MainProgram <ref_json> <targets.csv> <output.csv> <pcap_ref> <pcap_other>
 *               [--grid-res-m=0.1] [--max-scans=0]
 */

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pcap_data_manipulator.h"
#include "repeatability_filters.h"

namespace fs = std::filesystem;
using ouster::LidarScan;
using ouster::sensor::sensor_info;
using ouster::sensor_utils::PcapDataManipulator;
using ouster::sensor_utils::RepeatabilityPipeline;

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
    std::unordered_map<std::string, MeanResults> filt_by_name;
};

struct AppConfig {
    std::string json_path;
    std::string targets_path;
    std::string out_path;
    std::string pcap_ref;
    std::string pcap_other;
    double grid_res_m = 0.1;
    size_t max_scans = 0;
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

std::vector<TargetPoint> load_targets(const std::string& path, double grid_res_m) {
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
        while (std::getline(ss, item, ',')) cols.push_back(trim(item));
        if (cols.size() < 2) continue;

        TargetPoint t;
        size_t idx = 0;
        if (cols.size() == 2) {
            t.id = std::to_string(targets.size());
        } else {
            t.id = cols[idx++];
        }
        try {
            t.x = std::stod(cols[idx++]);
            t.y = std::stod(cols[idx++]);
            t.key = key_from_xy(t.x, t.y, grid_res_m);
            targets.push_back(t);
        } catch (...) {
            std::cerr << "Skipping line " << line_no << " (bad number): " << line << "\n";
            continue;
        }
    }
    return targets;
}

class DataContext {
   public:
    DataContext(std::string pcap, std::string json) : pcap_(std::move(pcap)), json_(std::move(json)) {}
    bool load() {
        if (!manip_.load_metadata(json_)) {
            std::cerr << "Failed to load metadata: " << json_ << "\n";
            return false;
        }
        const int scans = manip_.load_pcap(pcap_);
        if (scans <= 0) {
            std::cerr << "Failed to load PCAP: " << pcap_ << "\n";
            return false;
        }
        return true;
    }
    PcapDataManipulator& manip() { return manip_; }
    const sensor_info& info() const { return manip_.get_sensor_info(); }

   private:
    std::string pcap_;
    std::string json_;
    PcapDataManipulator manip_;
};

struct PipelineSpec {
    std::string name;
    std::function<RepeatabilityPipeline(const sensor_info&)> builder;
};

std::vector<PipelineSpec> make_pipeline_specs() {
    return {
        {"kalman", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(50.0, 200.0));
             return p;
         }},
        {"outlier", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(2.5, 1500));
             return p;
         }},
        {"planarity", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(1, 150, 0.6));
             return p;
         }},
        {"normal", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(12.0, 0.6));
             return p;
         }},
        {"full_chain", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(50.0, 200.0));
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(2.5, 1500));
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(1, 150, 0.6));
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(12.0, 0.6));
             return p;
         }},
    };
}

void accumulate_scan(const LidarScan& scan,
                     const ouster::XYZLut& lut,
                     double grid_res_m,
                     double fallback_radius_m,
                     const std::vector<TargetPoint>& targets,
                     const std::unordered_map<int64_t, size_t>& key_to_idx,
                     std::vector<double>& sum_z,
                     std::vector<size_t>& counts) {
    auto range = scan.field<uint32_t>(ouster::sensor::ChanField::RANGE);
    auto points = ouster::cartesian(scan, lut);
    const size_t w = static_cast<size_t>(scan.w);

    for (Eigen::Index i = 0; i < points.rows(); ++i) {
        const size_t row = static_cast<size_t>(i) / w;
        const size_t col = static_cast<size_t>(i) % w;
        if (range(static_cast<int>(row), static_cast<int>(col)) == 0) continue;

        const double x = points(i, 0);
        const double y = points(i, 1);
        const double z = points(i, 2);
        const long long gx = static_cast<long long>(std::llround(x / grid_res_m));
        const long long gy = static_cast<long long>(std::llround(y / grid_res_m));
        // Try a slightly larger grid neighborhood to reduce miss rate on targets.
        bool matched = false;
        for (int dx = -4; dx <= 4 && !matched; ++dx) {
            for (int dy = -4; dy <= 4 && !matched; ++dy) {
                const auto key = pack_key(gx + dx, gy + dy);
                const auto it = key_to_idx.find(key);
                if (it != key_to_idx.end()) {
                    sum_z[it->second] += z;
                    counts[it->second] += 1;
                    matched = true;
                }
            }
        }
        if (!matched && fallback_radius_m > 0.0) {
            // Fallback to a radial search in XY if the grid lookup missed.
            const double r2 = fallback_radius_m * fallback_radius_m;
            for (size_t ti = 0; ti < targets.size(); ++ti) {
                const double dx = x - targets[ti].x;
                const double dy = y - targets[ti].y;
                if (dx * dx + dy * dy <= r2) {
                    sum_z[ti] += z;
                    counts[ti] += 1;
                    break;
                }
            }
        }
    }
}

MeanResults compute_means(const std::vector<LidarScan>& scans,
                          size_t frames_to_use,
                          const ouster::XYZLut& lut,
                          const std::vector<TargetPoint>& targets,
                          const std::unordered_map<int64_t, size_t>& key_to_idx,
                          double grid_res_m) {
    MeanResults res;
    const size_t target_count = targets.size();
    res.mean.assign(target_count, std::numeric_limits<double>::quiet_NaN());
    res.counts.assign(target_count, 0);

    std::vector<double> sum_z(target_count, 0.0);
    const size_t max_frames = frames_to_use == 0 ? scans.size()
                                                 : std::min(frames_to_use, scans.size());
    for (size_t i = 0; i < max_frames; ++i) {
        accumulate_scan(scans[i], lut, grid_res_m, grid_res_m * 5.0,
                        targets, key_to_idx, sum_z, res.counts);
    }
    res.frames_used = max_frames;
    for (size_t i = 0; i < target_count; ++i) {
        if (res.counts[i] > 0) {
            res.mean[i] = sum_z[i] / static_cast<double>(res.counts[i]);
        }
    }
    return res;
}

AcquisitionResult evaluate_acquisition(const std::string& label,
                                       DataContext& ctx,
                                       const std::vector<TargetPoint>& targets,
                                       const std::unordered_map<int64_t, size_t>& key_to_idx,
                                       double grid_res_m,
                                       size_t max_scans,
                                       bool run_filters) {
    auto& manip = ctx.manip();
    std::vector<LidarScan> scans;
    scans.reserve(manip.num_scans());
    for (const auto& ref : manip.get_all_scans()) scans.push_back(ref);

    const size_t frames_to_use =
        max_scans == 0 ? scans.size() : std::min(max_scans, scans.size());
    const auto lut = ouster::make_xyz_lut(ctx.info(), true);

    AcquisitionResult res;
    res.label = label;
    res.raw = compute_means(scans, frames_to_use, lut, targets, key_to_idx, grid_res_m);

    if (run_filters) {
        const auto specs = make_pipeline_specs();
        for (const auto& spec : specs) {
            RepeatabilityPipeline p = spec.builder(ctx.info());
            auto filtered = p.run(scans);
            res.filt_by_name[spec.name] =
                compute_means(filtered, frames_to_use, lut, targets, key_to_idx, grid_res_m);
        }
    }
    return res;
}

bool write_comparison_csv(const std::string& out_path,
                          const std::vector<TargetPoint>& targets,
                          const AcquisitionResult& ref,
                          const AcquisitionResult& cand) {
    fs::path primary(out_path);
    std::ofstream ofs(primary, std::ios::trunc);
    if (!ofs.is_open()) {
        fs::path fallback = fs::current_path() / primary.filename();
        std::cerr << "Cannot open output for writing: " << primary
                  << " ; trying fallback " << fallback << "\n";
        ofs.open(fallback, std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "Cannot open fallback output either: " << fallback << "\n";
            return false;
        }
    }
    ofs << "point_id,x_m,y_m,z_ref_m,z_raw_m,dz_raw_m,missing_raw,raw_samples,frames_used";
    // columns per filter
    std::vector<std::string> pipelines;
    for (const auto& kv : cand.filt_by_name) pipelines.push_back(kv.first);
    std::sort(pipelines.begin(), pipelines.end());
    for (const auto& name : pipelines) {
        ofs << ",z_filt_" << name
            << ",dz_filt_" << name
            << ",missing_filt_" << name
            << ",filt_samples_" << name;
    }
    ofs << "\n";
    ofs << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < targets.size(); ++i) {
        const bool ref_missing =
            i >= ref.raw.counts.size() || ref.raw.counts[i] == 0 ||
            std::isnan(ref.raw.mean[i]);
        const bool raw_missing =
            i >= cand.raw.counts.size() || cand.raw.counts[i] == 0 ||
            std::isnan(cand.raw.mean[i]);

        const double z_ref =
            ref_missing ? std::numeric_limits<double>::quiet_NaN() : ref.raw.mean[i];
        const double z_raw =
            raw_missing ? std::numeric_limits<double>::quiet_NaN() : cand.raw.mean[i];
        const double dz_raw =
            (ref_missing || raw_missing) ? std::numeric_limits<double>::quiet_NaN()
                                         : (z_raw - z_ref);

        ofs << targets[i].id << ","
            << targets[i].x << "," << targets[i].y << ","
            << z_ref << "," << z_raw << ","
            << dz_raw << ","
            << (raw_missing ? 1 : 0) << ","
            << (i < cand.raw.counts.size() ? cand.raw.counts[i] : 0) << ","
            << cand.raw.frames_used;

        for (const auto& name : pipelines) {
            const auto it = cand.filt_by_name.find(name);
            const auto& filt_res = (it != cand.filt_by_name.end()) ? it->second : cand.raw;
            const bool filt_missing =
                i >= filt_res.counts.size() || filt_res.counts[i] == 0 ||
                std::isnan(filt_res.mean[i]);
            const double z_filt =
                filt_missing ? std::numeric_limits<double>::quiet_NaN()
                             : filt_res.mean[i];
            const double dz_filt =
                (ref_missing || filt_missing) ? std::numeric_limits<double>::quiet_NaN()
                                              : (z_filt - z_ref);
            ofs << "," << z_filt
                << "," << dz_filt
                << "," << (filt_missing ? 1 : 0)
                << "," << (i < filt_res.counts.size() ? filt_res.counts[i] : 0);
        }
        ofs << "\n";
    }
    return true;
}

AppConfig parse_args(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "Usage: MainProgram <ref_json> <targets.csv> <output.csv> <pcap_ref> <pcap_other> "
                     "[--grid-res-m=0.1] [--max-scans=0]\n";
        std::exit(EXIT_FAILURE);
    }
    AppConfig cfg;
    cfg.json_path = argv[1];
    cfg.targets_path = argv[2];
    cfg.out_path = argv[3];
    cfg.pcap_ref = argv[4];
    cfg.pcap_other = argv[5];
    for (int i = 6; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--grid-res-m=", 0) == 0) {
            try {
                cfg.grid_res_m = std::stod(arg.substr(std::string("--grid-res-m=").size()));
            } catch (...) {
                std::cerr << "Invalid --grid-res-m value; using default 0.1\n";
                cfg.grid_res_m = 0.1;
            }
        } else if (arg.rfind("--max-scans=", 0) == 0) {
            try {
                cfg.max_scans = static_cast<size_t>(
                    std::stoul(arg.substr(std::string("--max-scans=").size())));
            } catch (...) {
                std::cerr << "Invalid --max-scans value; using default 0\n";
                cfg.max_scans = 0;
            }
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    try {
        const AppConfig cfg = parse_args(argc, argv);

        auto targets = load_targets(cfg.targets_path, cfg.grid_res_m);
        if (targets.empty()) {
            std::cerr << "No valid targets loaded from " << cfg.targets_path << "\n";
            return EXIT_FAILURE;
        }

        std::unordered_map<int64_t, size_t> key_to_idx;
        for (size_t i = 0; i < targets.size(); ++i) key_to_idx[targets[i].key] = i;

        DataContext ref_ctx(cfg.pcap_ref, cfg.json_path);
        DataContext cand_ctx(cfg.pcap_other, cfg.json_path);
        std::cout << "Loading reference PCAP...\n";
        if (!ref_ctx.load()) return EXIT_FAILURE;
        std::cout << "Loading candidate PCAP...\n";
        if (!cand_ctx.load()) return EXIT_FAILURE;

        auto ref_res = evaluate_acquisition("ref", ref_ctx, targets, key_to_idx,
                                            cfg.grid_res_m, cfg.max_scans, false);
        auto cand_res = evaluate_acquisition("candidate", cand_ctx, targets, key_to_idx,
                                             cfg.grid_res_m, cfg.max_scans, true);

        // Ensure output directory exists (or fallback to current dir)
        fs::path out_path = cfg.out_path;
        fs::path out_dir = out_path.parent_path();
        if (out_dir.empty()) out_dir = fs::current_path();
        std::error_code ec;
        fs::create_directories(out_dir, ec);
        if (ec) {
            std::cerr << "Warning: failed to create output dir " << out_dir.string()
                      << ", using current directory.\n";
            out_path = fs::current_path() / out_path.filename();
        }

        if (!write_comparison_csv(out_path.string(), targets, ref_res, cand_res)) {
            std::cerr << "Failed to write output CSV: " << out_path << "\n";
            return EXIT_FAILURE;
        }
        std::cout << "Wrote comparison CSV to " << out_path << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
