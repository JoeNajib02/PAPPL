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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <limits>
#include <map>
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
    // Coordinates used for matching against point clouds (may be snapped).
    double x = 0.0;
    double y = 0.0;
    double z = std::numeric_limits<double>::quiet_NaN();  // z-hint for matching (optional)

    // Original coordinates as provided by the input targets file (kept for reporting).
    double x_report = 0.0;
    double y_report = 0.0;

    bool has_z_ref_override = false;
    bool has_z_raw_override = false;
    double z_ref_override = std::numeric_limits<double>::quiet_NaN();
    double z_raw_override = std::numeric_limits<double>::quiet_NaN();
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
    double mean_dz_m = std::numeric_limits<double>::quiet_NaN();
    double mean_abs_dz_m = std::numeric_limits<double>::quiet_NaN();
    double max_abs_dz_m = std::numeric_limits<double>::quiet_NaN();
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
    std::string profile_dir;           // optional profile directory for nearest Z lookup
    double fallback_radius_m = 3.0;    // extra radial search if grid misses
    double max_match_radius_m = 0.005;  // 5 mm hard cap by default
    double snap_ref_radius_m = 0.0;     // if >0, snap target XY/Z to ref PCAP first
    size_t snap_ref_scans = 0;          // 0 = all

    // Manual rails mode: parse 12 whitespace-separated numbers (x1 y1 z1 x2 y2 z2) for two rails.
    // Points are generated every point_step_m between the endpoints (start + regular samples + end).
    std::string manual_rails = "";    // e.g. "-4.515 -11.886 -1.892 0.575 32.714 -2.072  -2.975 -11.986 -1.982 1.965 31.644 -1.962"
    double point_step_m = 3.0;
    bool dry_run = false; // if true, print generated targets and exit
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
        const double dz_m = (cmp.mean[i] - ref.mean[i]);
        s.samples++;
        sum += dz_m;
        const double adz = std::abs(dz_m);
        sum_abs += adz;
        if (adz > max_abs) max_abs = adz;
    }
    if (s.samples > 0) {
        s.mean_dz_m = sum / static_cast<double>(s.samples);
        s.mean_abs_dz_m = sum_abs / static_cast<double>(s.samples);
        s.max_abs_dz_m = max_abs;
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
    double unit_scale = 1.0;  // assume meters unless we detect *_mm headers
    bool unit_scale_set = false;
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

        // Detect and honor headers that indicate millimeter units.
        if (!unit_scale_set) {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (lower.find("_mm") != std::string::npos) {
                unit_scale = 0.001;
                unit_scale_set = true;
                continue;  // header line, skip parsing
            }
        }

        TargetPoint t;
        size_t idx = 0;
        if (cols.size() == 2) {
            t.id = std::to_string(targets.size());
        } else {
            t.id = cols[idx++];
        }
        try {
            t.x = std::stod(cols[idx++]) * unit_scale;
            t.y = std::stod(cols[idx++]) * unit_scale;
            t.x_report = t.x;
            t.y_report = t.y;
            const size_t remaining = cols.size() > idx ? cols.size() - idx : 0;
            if (remaining >= 3) {
                // Interpret as: z, z_ref, z_raw
                t.z = std::stod(cols[idx++]) * unit_scale;
                t.z_ref_override = std::stod(cols[idx++]) * unit_scale;
                t.has_z_ref_override = true;
                t.z_raw_override = std::stod(cols[idx++]) * unit_scale;
                t.has_z_raw_override = true;
            } else if (remaining == 2) {
                // Interpret as: z_ref, z_raw (no z column provided)
                t.z_ref_override = std::stod(cols[idx++]) * unit_scale;
                t.has_z_ref_override = true;
                t.z_raw_override = std::stod(cols[idx++]) * unit_scale;
                t.has_z_raw_override = true;
                // Use z_ref as approximate z for matching if present.
                t.z = t.z_ref_override;
            } else if (remaining == 1) {
                // Only z provided
                t.z = std::stod(cols[idx++]) * unit_scale;
            }
            t.key = key_from_xy(t.x, t.y, grid_res_m);
            targets.push_back(t);
        } catch (...) {
            // Treat non-numeric rows as headers without spamming stderr.
            bool has_alpha = false;
            for (const auto& c : cols) {
                for (char ch : c) {
                    if (std::isalpha(static_cast<unsigned char>(ch))) {
                        has_alpha = true;
                        break;
                    }
                }
                if (has_alpha) break;
            }
            if (has_alpha) {
                continue;
            }
            std::cerr << "Skipping line " << line_no << " (bad number): " << line << "\n";
            continue;
        }
    }
    return targets;
}

bool parse_manual_rails(const std::string& s, std::vector<double>& out) {
    out.clear();
    std::stringstream ss(s);
    double v = 0.0;
    while (ss >> v) out.push_back(v);
    return out.size() == 12; // two rails x (x1 y1 z1 x2 y2 z2) each
}

std::vector<TargetPoint> generate_targets_from_manual_rails(const std::string& manual,
                                                            double step_m,
                                                            double grid_res_m) {
    std::vector<TargetPoint> out;
    std::vector<double> vals;
    if (!parse_manual_rails(manual, vals)) {
        std::cerr << "Failed to parse --manual-rails (expect 12 whitespace numbers)." << std::endl;
        return out;
    }

    auto append_points = [&](const std::array<double,3>& a,
                             const std::array<double,3>& b,
                             const std::string& prefix) {
        const double dx = b[0] - a[0];
        const double dy = b[1] - a[1];
        const double dz = b[2] - a[2];
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist <= 1e-9) return;

        const double ux = dx / dist;
        const double uy = dy / dist;
        const double uz = dz / dist;

        auto make_point = [&](double s, const std::string& label) {
            TargetPoint t;
            t.id = label;
            t.x = a[0] + ux * s;
            t.y = a[1] + uy * s;
            t.z = a[2] + uz * s;
            t.x_report = t.x;
            t.y_report = t.y;
            t.key = key_from_xy(t.x, t.y, grid_res_m);
            out.push_back(t);
        };

        // Start
        make_point(0.0, prefix + "_start");
        // Regular 3 m spaced samples (or user-specified step)
        const int steps = static_cast<int>(std::floor(dist / step_m));
        for (int i = 1; i <= steps; ++i) {
            const double s = step_m * static_cast<double>(i);
            if (s >= dist - 1e-6) break;  // avoid duplicating the end point
            const int label_m = static_cast<int>(std::llround(s));
            make_point(s, prefix + "_" + std::to_string(label_m) + "m");
        }
        // End
        make_point(dist, prefix + "_end");
    };

    // two rails: R1 (0..5), R2 (6..11)
    std::array<double,3> r1a{vals[0], vals[1], vals[2]};
    std::array<double,3> r1b{vals[3], vals[4], vals[5]};
    std::array<double,3> r2a{vals[6], vals[7], vals[8]};
    std::array<double,3> r2b{vals[9], vals[10], vals[11]};

    append_points(r1a, r1b, "R1");
    append_points(r2a, r2b, "R2");

    std::cout << "Generated " << out.size() << " targets from manual rails." << std::endl;
    return out;
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
             // Stronger temporal smoothing (larger process/measurement noise).
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(20000.0, 8000.0));
             return p;
         }},
        {"outlier", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Mild outlier rejection: moderate z-score, small neighbor count to preserve sparse data.
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(2.5, 8));
             return p;
         }},
        {"planarity", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Smaller neighborhood and softer planarity threshold to avoid wiping sparse rails.
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(3, 60, 0.35));
             return p;
         }},
        {"normal", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             // Moderate angular match, balanced blending.
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(7.5, 0.7));
             return p;
         }},
        {"full_chain", [](const sensor_info& info) {
             RepeatabilityPipeline p(info);
             p.add_filter(std::make_unique<ouster::sensor_utils::KalmanRangeFilter>(20000.0, 8000.0));
             p.add_filter(std::make_unique<ouster::sensor_utils::StatisticalOutlierFilter>(2.5, 8));
             p.add_filter(std::make_unique<ouster::sensor_utils::PlanaritySmoother>(3, 60, 0.35));
             p.add_filter(std::make_unique<ouster::sensor_utils::NormalGuidedSmoother>(7.5, 0.7));
             return p;
         }},
    };
}

void accumulate_scan(const LidarScan& scan,
                     const ouster::XYZLut& lut,
                     double grid_res_m,
                     double fallback_radius_m,
                     double max_match_r2,
                     const std::vector<TargetPoint>& targets,
                     const std::unordered_map<int64_t, size_t>& key_to_idx,
                     std::vector<double>& best_z,
                     std::vector<double>& best_score,
                     double z_hint_weight = 1.0) {
    auto range = scan.field<uint32_t>(ouster::sensor::ChanField::RANGE);
    auto points = ouster::cartesian(scan, lut);
    const size_t w = static_cast<size_t>(scan.w);
    const double fallback_r2 = fallback_radius_m > 0.0
                                   ? fallback_radius_m * fallback_radius_m
                                   : std::numeric_limits<double>::infinity();

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
        for (int dx = -6; dx <= 6; ++dx) {
            for (int dy = -6; dy <= 6; ++dy) {
                const auto key = pack_key(gx + dx, gy + dy);
                const auto it = key_to_idx.find(key);
                if (it != key_to_idx.end()) {
                    const size_t idx = it->second;
                    const double tx = targets[idx].x;
                    const double ty = targets[idx].y;
                    const double d2_xy = (x - tx) * (x - tx) + (y - ty) * (y - ty);
                    if (d2_xy > fallback_r2 || d2_xy > max_match_r2) continue;
                    double score = d2_xy;
                    if (!std::isnan(targets[idx].z)) {
                        const double dz = z - targets[idx].z;
                        score += z_hint_weight * dz * dz;
                    }
                    if (score < best_score[idx]) {
                        best_score[idx] = score;
                        best_z[idx] = z;
                    }
                }
            }
        }
        if (fallback_radius_m > 0.0) {
            // Fallback to a radial search in XY, constrained by fallback radius and max match.
            for (size_t ti = 0; ti < targets.size(); ++ti) {
                const double dx = x - targets[ti].x;
                const double dy = y - targets[ti].y;
                const double d2_xy = dx * dx + dy * dy;
                if (d2_xy > fallback_r2 || d2_xy > max_match_r2) continue;
                double score = d2_xy;
                if (!std::isnan(targets[ti].z)) {
                    const double dz = z - targets[ti].z;
                    score += z_hint_weight * dz * dz;
                }
                if (score < best_score[ti]) {
                    best_score[ti] = score;
                    best_z[ti] = z;
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
                          double fallback_radius_m,
                          double max_match_radius_m) {
    MeanResults res;
    const size_t target_count = targets.size();
    res.mean.assign(target_count, std::numeric_limits<double>::quiet_NaN());
    res.counts.assign(target_count, 0);

    std::vector<double> sum_z(target_count, 0.0);
    const size_t max_frames = frames_to_use == 0 ? scans.size()
                                                 : std::min(frames_to_use, scans.size());
    const double max_match_r2 = max_match_radius_m <= 0.0
                                    ? std::numeric_limits<double>::infinity()
                                    : max_match_radius_m * max_match_radius_m;
    for (size_t i = 0; i < max_frames; ++i) {
        std::vector<double> best_z(target_count, std::numeric_limits<double>::quiet_NaN());
        std::vector<double> best_score(target_count, std::numeric_limits<double>::infinity());
        accumulate_scan(scans[i], lut, grid_res_m, fallback_radius_m, max_match_r2,
                        targets, key_to_idx, best_z, best_score, 1.0);
        for (size_t ti = 0; ti < target_count; ++ti) {
            if (!std::isnan(best_z[ti]) && std::isfinite(best_score[ti])) {
                sum_z[ti] += best_z[ti];
                res.counts[ti] += 1;
            }
        }
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
                                       double max_match_radius_m,
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
                            grid_res_m, fallback_radius_m, max_match_radius_m);

    if (run_filters) {
        const auto specs = make_pipeline_specs();
        for (const auto& spec : specs) {
            RepeatabilityPipeline p = spec.builder(ctx.info());
            auto filtered = p.run(scans);
            res.filt_by_name[spec.name] =
                compute_means(filtered, frames_to_use, lut, targets, key_to_idx,
                              grid_res_m, fallback_radius_m, max_match_radius_m);
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

struct SnapResult {
    bool found = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double score = std::numeric_limits<double>::infinity();
    double dist_xy2 = std::numeric_limits<double>::infinity();
};

std::vector<SnapResult> snap_targets_to_ref(DataContext& ctx,
                                            const std::vector<TargetPoint>& targets,
                                            double snap_radius_m,
                                            size_t max_scans) {
    const auto& info = ctx.info();
    const auto lut = ouster::make_xyz_lut(info, true);
    auto& manip = ctx.manip();

    std::vector<SnapResult> snaps(targets.size());
    const double r2 = snap_radius_m > 0.0 ? snap_radius_m * snap_radius_m
                                          : std::numeric_limits<double>::infinity();

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
                const double dx = x - targets[ti].x;
                const double dy = y - targets[ti].y;
                const double d2_xy = dx * dx + dy * dy;
                if (d2_xy > r2) continue;
                double score = d2_xy;
                if (std::isfinite(targets[ti].z)) {
                    const double dz = z - targets[ti].z;
                    score += dz * dz;
                }
                if (score < snaps[ti].score) {
                    snaps[ti].found = true;
                    snaps[ti].x = x;
                    snaps[ti].y = y;
                    snaps[ti].z = z;
                    snaps[ti].score = score;
                    snaps[ti].dist_xy2 = d2_xy;
                }
            }
        }
    }

    size_t found = 0;
    double sum_dist = 0.0;
    for (const auto& s : snaps) {
        if (s.found) {
            found++;
            sum_dist += std::sqrt(s.dist_xy2);
        }
    }
    std::cout << "[snap] snapped " << found << "/" << targets.size()
              << " targets using " << limit << " frames";
    if (found > 0) std::cout << ", mean_xy=" << (sum_dist / static_cast<double>(found)) << " m";
    std::cout << "\n";
    return snaps;
}

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
        ofs << ",z_filt_" << name << "_m"
            << ",dz_filt_" << name << "_m"
            << ",missing_filt_" << name
            << ",filt_samples_" << name;
    }
    ofs << ",z_filt_best_m,dz_filt_best_m,filt_best_name";
    ofs << "\n";
    ofs << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < targets.size(); ++i) {
        const bool ref_missing_orig =
            i >= ref.raw.counts.size() || ref.raw.counts[i] == 0 ||
            std::isnan(ref.raw.mean[i]);
        const bool raw_missing_orig =
            i >= cand.raw.counts.size() || cand.raw.counts[i] == 0 ||
            std::isnan(cand.raw.mean[i]);

        bool ref_missing = ref_missing_orig;
        bool raw_missing = raw_missing_orig;

        double z_ref =
            ref_missing ? std::numeric_limits<double>::quiet_NaN() : ref.raw.mean[i];
        double z_raw =
            raw_missing ? std::numeric_limits<double>::quiet_NaN() : cand.raw.mean[i];

        if (targets[i].has_z_ref_override) {
            z_ref = targets[i].z_ref_override;
            ref_missing = false;
        }
        if (targets[i].has_z_raw_override) {
            z_raw = targets[i].z_raw_override;
            raw_missing = false;
        }
        const double dz_raw =
            (ref_missing || raw_missing) ? std::numeric_limits<double>::quiet_NaN()
                                         : (z_raw - z_ref);

        ofs << targets[i].id << ","
            << targets[i].x_report << "," << targets[i].y_report << ","
            << z_ref << "," << z_raw << ","
            << dz_raw << ","
            << (raw_missing ? 1 : 0) << ","
            << (i < cand.raw.counts.size() ? cand.raw.counts[i] : 0) << ","
            << cand.raw.frames_used;

        double best_z = std::numeric_limits<double>::quiet_NaN();
        double best_dz = std::numeric_limits<double>::quiet_NaN();
        std::string best_name;
        double best_abs_dz = std::numeric_limits<double>::infinity();

        for (const auto& name : pipelines) {
            const auto it = cand.filt_by_name.find(name);
            const auto& filt_res = (it != cand.filt_by_name.end()) ? it->second : cand.raw;
            bool filt_missing =
                i >= filt_res.counts.size() || filt_res.counts[i] == 0 ||
                std::isnan(filt_res.mean[i]);
            double z_filt =
                filt_missing ? std::numeric_limits<double>::quiet_NaN()
                             : filt_res.mean[i];
            const double dz_filt =
                (ref_missing || filt_missing) ? std::numeric_limits<double>::quiet_NaN()
                                              : (z_filt - z_ref);
            ofs << "," << z_filt
                << "," << dz_filt
                << "," << (filt_missing ? 1 : 0)
                << "," << (i < filt_res.counts.size() ? filt_res.counts[i] : 0);

            if (!filt_missing && !ref_missing) {
                const double abs_dz = std::abs(dz_filt);
                if (abs_dz < best_abs_dz) {
                    best_abs_dz = abs_dz;
                    best_z = z_filt;
                    best_dz = dz_filt;
                    best_name = name;
                }
            }
        }
        ofs << "," << best_z << "," << best_dz << "," << best_name;
        ofs << "\n";
    }
    return true;
}

bool write_filtered_csv(const fs::path& out_path,
                        const std::vector<TargetPoint>& targets,
                        const AcquisitionResult& ref,
                        const AcquisitionResult& cand) {
    fs::path out = out_path.parent_path() / (out_path.stem().string() + "_filtered.csv");
    std::ofstream ofs(out, std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "Warning: cannot open filtered output: " << out << "\n";
        return false;
    }
    std::vector<std::string> pipelines;
    for (const auto& kv : cand.filt_by_name) pipelines.push_back(kv.first);
    std::sort(pipelines.begin(), pipelines.end());

    ofs << "point_id,x_m,y_m,z_ref_m,z_raw_m";
    for (const auto& name : pipelines) ofs << ",z_filt_" << name << "_m";
    ofs << ",z_filt_best_m,filt_best_name";
    ofs << "\n";
    ofs << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < targets.size(); ++i) {
        const bool ref_missing =
            i >= ref.raw.counts.size() || ref.raw.counts[i] == 0 ||
            std::isnan(ref.raw.mean[i]);
        const bool raw_missing =
            i >= cand.raw.counts.size() || cand.raw.counts[i] == 0 ||
            std::isnan(cand.raw.mean[i]);

        double z_ref = ref_missing ? std::numeric_limits<double>::quiet_NaN() : ref.raw.mean[i];
        double z_raw = raw_missing ? std::numeric_limits<double>::quiet_NaN() : cand.raw.mean[i];
        bool ref_missing_eff = ref_missing;
        if (targets[i].has_z_ref_override) {
            z_ref = targets[i].z_ref_override;
            ref_missing_eff = false;
        }
        if (targets[i].has_z_raw_override) {
            z_raw = targets[i].z_raw_override;
        }

        ofs << targets[i].id << "," << targets[i].x_report << "," << targets[i].y_report << ","
            << z_ref << "," << z_raw;

        double best_z = std::numeric_limits<double>::quiet_NaN();
        std::string best_name;
        double best_abs_dz = std::numeric_limits<double>::infinity();

        for (const auto& name : pipelines) {
            const auto it = cand.filt_by_name.find(name);
            const auto& filt_res = (it != cand.filt_by_name.end()) ? it->second : cand.raw;
            bool filt_missing =
                i >= filt_res.counts.size() || filt_res.counts[i] == 0 ||
                std::isnan(filt_res.mean[i]);
            double z_filt =
                filt_missing ? std::numeric_limits<double>::quiet_NaN()
                             : filt_res.mean[i];
            ofs << "," << z_filt;

            if (!filt_missing && !ref_missing_eff) {
                const double abs_dz = std::abs(z_filt - z_ref);
                if (abs_dz < best_abs_dz) {
                    best_abs_dz = abs_dz;
                    best_z = z_filt;
                    best_name = name;
                }
            }
        }
        ofs << "," << best_z << "," << best_name;
        ofs << "\n";
    }
    std::cout << "Wrote filtered CSV to " << out << "\n";
    return true;
}

AppConfig parse_args(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "Usage: MainProgram <ref_json> <targets.csv> <output.csv> <pcap_ref> <pcap_other> "
                     "[--grid-res-m=0.1] [--max-scans=0] [--fallback-m=3.0] [--max-match-m=0.005]\n"
                  << "       (optional) --manual-rails=\"<12 nums>\" --point-step-m=3.0 --profile-dir=<path>\n";
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
        } else if (arg.rfind("--max-match-m=", 0) == 0) {
            try {
                cfg.max_match_radius_m =
                    std::stod(arg.substr(std::string("--max-match-m=").size()));
            } catch (...) {
                std::cerr << "Invalid --max-match-m value; using default 0.005\n";
                cfg.max_match_radius_m = 0.005;
            }
        } else if (arg.rfind("--manual-rails=", 0) == 0) {
            cfg.manual_rails = arg.substr(std::string("--manual-rails=").size());
        } else if (arg.rfind("--point-step-m=", 0) == 0) {
            try {
                cfg.point_step_m = std::stod(arg.substr(std::string("--point-step-m=").size()));
                if (cfg.point_step_m <= 0.0) cfg.point_step_m = 3.0;
            } catch (...) {
                std::cerr << "Invalid --point-step-m value; using default 3.0\n";
                cfg.point_step_m = 3.0;
            }
        } else if (arg == "--dry-run") {
            cfg.dry_run = true;
        } else if (arg.rfind("--profile-dir=", 0) == 0) {
            cfg.profile_dir = arg.substr(std::string("--profile-dir=").size());
        } else if (arg.rfind("--snap-ref-m=", 0) == 0) {
            try {
                cfg.snap_ref_radius_m =
                    std::stod(arg.substr(std::string("--snap-ref-m=").size()));
                if (cfg.snap_ref_radius_m < 0.0) cfg.snap_ref_radius_m = 0.0;
            } catch (...) {
                std::cerr << "Invalid --snap-ref-m value; disabling snapping\n";
                cfg.snap_ref_radius_m = 0.0;
            }
        } else if (arg.rfind("--snap-ref-scans=", 0) == 0) {
            try {
                cfg.snap_ref_scans =
                    static_cast<size_t>(std::stoul(arg.substr(std::string("--snap-ref-scans=").size())));
            } catch (...) {
                std::cerr << "Invalid --snap-ref-scans value; using 0 (all)\n";
                cfg.snap_ref_scans = 0;
            }
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    try {
        const AppConfig cfg = parse_args(argc, argv);

        std::vector<TargetPoint> targets;
        if (!cfg.manual_rails.empty()) {
            targets = generate_targets_from_manual_rails(cfg.manual_rails, cfg.point_step_m, cfg.grid_res_m);
            if (targets.empty()) {
                std::cerr << "No targets generated from --manual-rails. Exiting.\n";
                return EXIT_FAILURE;
            }
        } else {
            targets = load_targets(cfg.targets_path, cfg.grid_res_m);
            if (targets.empty()) {
                std::cerr << "No valid targets loaded from " << cfg.targets_path << "\n";
                return EXIT_FAILURE;
            }
        }

        if (cfg.dry_run) {
            std::cout << "Dry-run: generated targets:\n";
            for (const auto& t : targets) {
                std::cout << t.id << "," << t.x << "," << t.y << "," << t.z << "\n";
            }
            return EXIT_SUCCESS;
        }

        std::cout << "[config] grid_res=" << cfg.grid_res_m << " m"
                  << ", fallback=" << cfg.fallback_radius_m << " m"
                  << ", max_match=" << (cfg.max_match_radius_m <= 0 ? std::string("inf")
                                                                    : std::to_string(cfg.max_match_radius_m) + " m")
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

        if (cfg.snap_ref_radius_m > 0.0) {
            std::cout << "[snap] snapping targets to reference PCAP (r="
                      << cfg.snap_ref_radius_m << " m"
                      << ", scans=" << (cfg.snap_ref_scans == 0 ? std::string("all")
                                                                : std::to_string(cfg.snap_ref_scans))
                      << ")\n";
            const auto snaps = snap_targets_to_ref(ref_ctx, targets, cfg.snap_ref_radius_m, cfg.snap_ref_scans);
            fs::path snap_out = out_path.parent_path() / (out_path.stem().string() + "_targets_snapped_to_ref.csv");
            std::ofstream sfs(snap_out, std::ios::trunc);
            if (sfs.is_open()) {
                sfs << "point_id,x_report_m,y_report_m,x_snap_m,y_snap_m,z_snap_m,found,dist_xy_m\n";
                sfs << std::fixed << std::setprecision(6);
                for (size_t i = 0; i < targets.size(); ++i) {
                    sfs << targets[i].id << ","
                        << targets[i].x_report << "," << targets[i].y_report << ",";
                    if (snaps[i].found) {
                        sfs << snaps[i].x << "," << snaps[i].y << "," << snaps[i].z << ",0,"
                            << std::sqrt(snaps[i].dist_xy2) << "\n";
                    } else {
                        sfs << "nan,nan,nan,1,nan\n";
                    }
                }
                std::cout << "[snap] wrote " << snap_out << "\n";
            } else {
                std::cerr << "[snap] warning: failed to write " << snap_out << "\n";
            }
            for (size_t i = 0; i < targets.size(); ++i) {
                if (!snaps[i].found) continue;
                targets[i].x = snaps[i].x;
                targets[i].y = snaps[i].y;
                targets[i].z = snaps[i].z;
            }
        }

        // recompute keys with aligned targets
        for (auto& t : targets) t.key = key_from_xy(t.x, t.y, cfg.grid_res_m);

        std::unordered_map<int64_t, size_t> key_to_idx;
        for (size_t i = 0; i < targets.size(); ++i) key_to_idx[targets[i].key] = i;

        auto ref_res = evaluate_acquisition("ref", ref_ctx, targets, key_to_idx,
                                            cfg.grid_res_m, cfg.max_scans,
                                            cfg.fallback_radius_m, cfg.max_match_radius_m,
                                            false);
        auto cand_res = evaluate_acquisition("candidate", cand_ctx, targets, key_to_idx,
                                             cfg.grid_res_m, cfg.max_scans,
                                             cfg.fallback_radius_m, cfg.max_match_radius_m,
                                             true);

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
            std::cout << "  dz raw: mean=" << raw_stats.mean_dz_m << " m"
                      << ", mean|dz|=" << raw_stats.mean_abs_dz_m << " m"
                      << ", max|dz|=" << raw_stats.max_abs_dz_m << " m"
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
                std::cout << "    dz: mean=" << stats.mean_dz_m << " m"
                          << ", mean|dz|=" << stats.mean_abs_dz_m << " m"
                          << ", max|dz|=" << stats.max_abs_dz_m << " m"
                          << " over " << stats.samples << " samples\n";
            }
        }

        // Verify targets in the reference PCAP (closest hit within fallback radius).
        auto hits = verify_targets(ref_ctx, targets, cfg.grid_res_m,
                                   cfg.fallback_radius_m, cfg.max_scans);

        if (!write_comparison_csv(out_path.string(), targets, ref_res, cand_res)) {
            std::cerr << "Failed to write output CSV: " << out_path << "\n";
            return EXIT_FAILURE;
        }
        write_filtered_csv(out_path, targets, ref_res, cand_res);
        // Also dump a verification CSV for visibility.
        fs::path verify_path = out_path.parent_path() / "targets_verified_in_ref.csv";
        std::ofstream vfs(verify_path, std::ios::trunc);
        if (vfs.is_open()) {
            vfs << "id,x_tgt_m,y_tgt_m,x_hit_m,y_hit_m,z_hit_m,found,dist_xy_m\n";
            vfs << std::fixed << std::setprecision(6);
            for (size_t i = 0; i < targets.size(); ++i) {
                vfs << targets[i].id << ","
                    << targets[i].x << "," << targets[i].y << ",";
                if (hits[i].found) {
                    vfs << hits[i].x << "," << hits[i].y << ","
                        << hits[i].z << ",0,"
                        << std::sqrt(hits[i].dist) << "\n";
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
