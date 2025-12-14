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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>

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

struct TargetSummary {
    size_t found = 0;
    size_t missing = 0;
};

struct DeltaStats {
    size_t samples = 0;
    double mean_dz_mm = std::numeric_limits<double>::quiet_NaN();
    double mean_abs_dz_mm = std::numeric_limits<double>::quiet_NaN();
    double max_abs_dz_mm = std::numeric_limits<double>::quiet_NaN();
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
    double fallback_radius_m = 3.0;  // extra radial search if grid misses
};

DeltaStats compute_delta_stats(const MeanResults& ref, const MeanResults& cmp) {
    DeltaStats s{};
    const size_t n = std::min(ref.mean.size(), cmp.mean.size());
    double sum = 0.0;
    double sum_abs = 0.0;
    double max_abs = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const bool ref_missing =
            i >= ref.counts.size() || ref.counts[i] == 0 ||
            std::isnan(ref.mean[i]);
        const bool cmp_missing =
            i >= cmp.counts.size() || cmp.counts[i] == 0 ||
            std::isnan(cmp.mean[i]);
        if (ref_missing || cmp_missing) continue;
        const double dz_mm = (cmp.mean[i] - ref.mean[i]) * 1000.0;
        s.samples++;
        sum += dz_mm;
        const double adz = std::abs(dz_mm);
        sum_abs += adz;
        if (adz > max_abs) max_abs = adz;
    }
    if (s.samples > 0) {
        s.mean_dz_mm = sum / static_cast<double>(s.samples);
        s.mean_abs_dz_mm = sum_abs / static_cast<double>(s.samples);
        s.max_abs_dz_mm = max_abs;
    }
    return s;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
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
        for (auto& c : cols) c = strip_quotes(c);

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

TargetSummary summarize_targets(const MeanResults& res) {
    TargetSummary s{};
    const size_t n = res.mean.size();
    for (size_t i = 0; i < n; ++i) {
        const bool missing =
            i >= res.counts.size() || res.counts[i] == 0 ||
            std::isnan(res.mean[i]);
        if (missing) {
            s.missing++;
        } else {
            s.found++;
        }
    }
    return s;
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
             // More aggressive smoothing: higher process / measurement noise variances.
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(5000.0, 2000.0));
             return p;
         }},
        {"outlier", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Stricter outlier rejection: lower z-score, higher min range.
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(1.5, 1500));
             return p;
         }},
        {"planarity", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Larger neighborhood, tighter planarity threshold.
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(2, 500, 0.3));
             return p;
         }},
        {"normal", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Tighter angular match, heavier blend with neighbors.
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(8.0, 0.8));
             return p;
         }},
        {"full_chain", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(5000.0, 2000.0));
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(1.5, 1500));
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(2, 500, 0.3));
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(8.0, 0.8));
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
        for (int dx = -6; dx <= 6 && !matched; ++dx) {
            for (int dy = -6; dy <= 6 && !matched; ++dy) {
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
                          double grid_res_m,
                          double fallback_radius_m) {
    MeanResults res;
    const size_t target_count = targets.size();
    res.mean.assign(target_count, std::numeric_limits<double>::quiet_NaN());
    res.counts.assign(target_count, 0);

    std::vector<double> sum_z(target_count, 0.0);
    const size_t max_frames = frames_to_use == 0 ? scans.size()
                                                 : std::min(frames_to_use, scans.size());
    for (size_t i = 0; i < max_frames; ++i) {
        accumulate_scan(scans[i], lut, grid_res_m, fallback_radius_m,
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
                                       double fallback_radius_m,
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
    res.raw = compute_means(scans, frames_to_use, lut, targets, key_to_idx,
                            grid_res_m, fallback_radius_m);

    if (run_filters) {
        const auto specs = make_pipeline_specs();
        for (const auto& spec : specs) {
            RepeatabilityPipeline p = spec.builder(ctx.info());
            auto filtered = p.run(scans);
            res.filt_by_name[spec.name] =
                compute_means(filtered, frames_to_use, lut, targets, key_to_idx,
                              grid_res_m, fallback_radius_m);
        }
    }
    return res;
}

struct TargetHit {
    bool found = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double dist = std::numeric_limits<double>::infinity();
};

// Scan all points to find the closest hit for each target in the ref PCAP.
std::vector<TargetHit> verify_targets(DataContext& ctx,
                                      const std::vector<TargetPoint>& targets,
                                      double grid_res_m,
                                      double fallback_radius_m,
                                      size_t max_scans) {
    const auto& info = ctx.info();
    const auto lut = ouster::make_xyz_lut(info, true);
    auto& manip = ctx.manip();

    std::vector<TargetHit> hits(targets.size());

    // For speed, convert targets to simple arrays.
    std::vector<double> tx(targets.size()), ty(targets.size()), tz(targets.size());
    for (size_t i = 0; i < targets.size(); ++i) {
        tx[i] = targets[i].x;
        ty[i] = targets[i].y;
        tz[i] = 0.0;  // only need XY for matching, Z is reported from point
    }

    size_t frames_used = 0;
    const size_t limit = (max_scans == 0) ? manip.num_scans()
                                          : std::min(max_scans, manip.num_scans());
    for (size_t si = 0; si < limit; ++si) {
        auto scan = manip.get_scan(si);
        auto range = scan.field<uint32_t>(ouster::sensor::ChanField::RANGE);
        auto pts = ouster::cartesian(scan, lut);
        const size_t w = static_cast<size_t>(scan.w);
        for (Eigen::Index i = 0; i < pts.rows(); ++i) {
            const size_t row = static_cast<size_t>(i) / w;
            const size_t col = static_cast<size_t>(i) % w;
            if (range(static_cast<int>(row), static_cast<int>(col)) == 0) continue;
            const double x = pts(i, 0);
            const double y = pts(i, 1);
            const double z = pts(i, 2);
            for (size_t ti = 0; ti < targets.size(); ++ti) {
                const double dx = x - tx[ti];
                const double dy = y - ty[ti];
                const double d2 = dx * dx + dy * dy;
                if (d2 <= fallback_radius_m * fallback_radius_m && d2 < hits[ti].dist) {
                    hits[ti].found = true;
                    hits[ti].x = x;
                    hits[ti].y = y;
                    hits[ti].z = z;
                    hits[ti].dist = d2;
                }
            }
        }
        frames_used++;
    }

    std::cout << "[diag] target verification using " << frames_used << " frames\n";
    size_t missing = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (!hits[i].found) missing++;
    }
    if (missing > 0) {
        std::cout << "[diag] missing targets in ref: " << missing << "/" << targets.size()
                  << " (try larger --fallback-m or check alignment)\n";
    }
    return hits;
}

bool write_comparison_csv(const std::string& out_path,
                          const std::vector<TargetPoint>& targets,
                          const AcquisitionResult& ref,
                          const AcquisitionResult& cand) {
    const double kToMm = 1000.0;
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
    ofs << "point_id,x_mm,y_mm,z_ref_mm,z_raw_mm,dz_raw_mm,missing_raw,raw_samples,frames_used";
    // columns per filter
    std::vector<std::string> pipelines;
    for (const auto& kv : cand.filt_by_name) pipelines.push_back(kv.first);
    std::sort(pipelines.begin(), pipelines.end());
    for (const auto& name : pipelines) {
        ofs << ",z_filt_" << name << "_mm"
            << ",dz_filt_" << name << "_mm"
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
            << targets[i].x * kToMm << "," << targets[i].y * kToMm << ","
            << z_ref * kToMm << "," << z_raw * kToMm << ","
            << dz_raw * kToMm << ","
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
            ofs << "," << z_filt * kToMm
                << "," << dz_filt * kToMm
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
                     "[--grid-res-m=0.1] [--max-scans=0] [--fallback-m=3.0]\n";
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
        } else if (arg.rfind("--fallback-m=", 0) == 0) {
            try {
                cfg.fallback_radius_m = std::stod(arg.substr(std::string("--fallback-m=").size()));
            } catch (...) {
                std::cerr << "Invalid --fallback-m value; using default 3.0\n";
                cfg.fallback_radius_m = 3.0;
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

        std::cout << "[config] grid_res=" << cfg.grid_res_m << " m"
                  << ", fallback=" << cfg.fallback_radius_m << " m"
                  << ", max_scans=" << (cfg.max_scans == 0 ? std::string("all")
                                                          : std::to_string(cfg.max_scans))
                  << "\n";

        DataContext ref_ctx(cfg.pcap_ref, cfg.json_path);
        DataContext cand_ctx(cfg.pcap_other, cfg.json_path);
        std::cout << "Loading reference PCAP...\n";
        if (!ref_ctx.load()) return EXIT_FAILURE;
        std::cout << "[info] reference scans loaded: " << ref_ctx.manip().num_scans() << "\n";
        std::cout << "Loading candidate PCAP...\n";
        if (!cand_ctx.load()) return EXIT_FAILURE;
        std::cout << "[info] candidate scans loaded: " << cand_ctx.manip().num_scans() << "\n";

        // recompute keys with aligned targets
        for (auto& t : targets) t.key = key_from_xy(t.x, t.y, cfg.grid_res_m);

        std::unordered_map<int64_t, size_t> key_to_idx;
        for (size_t i = 0; i < targets.size(); ++i) key_to_idx[targets[i].key] = i;

        auto ref_res = evaluate_acquisition("ref", ref_ctx, targets, key_to_idx,
                                            cfg.grid_res_m, cfg.max_scans,
                                            cfg.fallback_radius_m, false);
        auto cand_res = evaluate_acquisition("candidate", cand_ctx, targets, key_to_idx,
                                             cfg.grid_res_m, cfg.max_scans,
                                             cfg.fallback_radius_m, true);

        // Print quick terminal summaries for visibility.
        const size_t total_targets = targets.size();
        auto raw_summary = summarize_targets(cand_res.raw);
        std::vector<std::string> pipeline_names;
        for (const auto& kv : cand_res.filt_by_name) pipeline_names.push_back(kv.first);
        std::sort(pipeline_names.begin(), pipeline_names.end());
        std::cout << "[summary] filters evaluated:";
        for (const auto& name : pipeline_names) std::cout << " " << name;
        std::cout << "\n";
        std::cout << "[summary] raw found " << raw_summary.found << "/" << total_targets
                  << ", missing " << raw_summary.missing
                  << ", frames used " << cand_res.raw.frames_used << "\n";
        auto raw_stats = compute_delta_stats(ref_res.raw, cand_res.raw);
        if (raw_stats.samples > 0) {
            std::cout << "  dz raw: mean=" << raw_stats.mean_dz_mm << " mm"
                      << ", mean|dz|=" << raw_stats.mean_abs_dz_mm << " mm"
                      << ", max|dz|=" << raw_stats.max_abs_dz_mm << " mm"
                      << " over " << raw_stats.samples << " samples\n";
        }
        for (const auto& name : pipeline_names) {
            const auto& res = cand_res.filt_by_name.at(name);
            auto s = summarize_targets(res);
            std::cout << "  - " << name << ": found " << s.found << "/" << total_targets
                      << ", missing " << s.missing
                      << ", frames used " << res.frames_used << "\n";
            auto stats = compute_delta_stats(ref_res.raw, res);
            if (stats.samples > 0) {
                std::cout << "    dz: mean=" << stats.mean_dz_mm << " mm"
                          << ", mean|dz|=" << stats.mean_abs_dz_mm << " mm"
                          << ", max|dz|=" << stats.max_abs_dz_mm << " mm"
                          << " over " << stats.samples << " samples\n";
            }
        }

        // Verify targets in the reference PCAP (closest hit within fallback radius).
        auto hits = verify_targets(ref_ctx, targets, cfg.grid_res_m,
                                   cfg.fallback_radius_m, cfg.max_scans);

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
        // Also dump a verification CSV for visibility.
        fs::path verify_path = out_path.parent_path() / "targets_verified_in_ref.csv";
        std::ofstream vfs(verify_path, std::ios::trunc);
        if (vfs.is_open()) {
            vfs << "id,x_tgt_mm,y_tgt_mm,x_hit_mm,y_hit_mm,z_hit_mm,found,dist_xy_mm\n";
            vfs << std::fixed << std::setprecision(6);
            const double kToMm = 1000.0;
            for (size_t i = 0; i < targets.size(); ++i) {
                vfs << targets[i].id << ","
                    << targets[i].x * kToMm << "," << targets[i].y * kToMm << ",";
                if (hits[i].found) {
                    vfs << hits[i].x * kToMm << "," << hits[i].y * kToMm << ","
                        << hits[i].z * kToMm << ",0,"
                        << std::sqrt(hits[i].dist) * kToMm << "\n";
                } else {
                    vfs << "nan,nan,nan,1,nan\n";
                }
            }
        } else {
            std::cerr << "Warning: failed to write verification CSV: "
                      << verify_path << "\n";
        }
        std::cout << "Wrote comparison CSV to " << out_path << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
