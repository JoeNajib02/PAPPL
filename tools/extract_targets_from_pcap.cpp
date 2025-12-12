/**
 * @file extract_targets_from_pcap.cpp
 * @brief Snap approximate targets to the nearest actual hits found in a PCAP.
 *
 * Usage:
 *   extract_targets_from_pcap <metadata.json> <pcap> <input_targets.csv> <output.csv>
 *                             [--fallback-m=0.3] [--max-scans=0]
 *
 * The input CSV must have at least id,x,y (z is ignored for matching).
 * The output CSV contains id,x_hit,y_hit,z_hit,found,dist_xy.
 */

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "pcap_data_manipulator.h"

using ouster::sensor_utils::PcapDataManipulator;

struct Target {
    std::string id;
    double x = 0.0;
    double y = 0.0;
};

struct Hit {
    bool found = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double dist2 = std::numeric_limits<double>::infinity();
};

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

std::vector<Target> load_targets(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open targets file: " + path);
    }
    std::vector<Target> out;
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
        if (cols.size() < 3) continue;

        for (auto& c : cols) c = strip_quotes(c);
        Target t;
        size_t idx = 0;
        // If first token is not a number, treat it as id.
        try {
            std::stod(cols[0]);
            t.id = std::to_string(out.size());
        } catch (...) {
            t.id = cols[idx++];
        }
        try {
            t.x = std::stod(cols[idx++]);
            t.y = std::stod(cols[idx++]);
            out.push_back(t);
        } catch (...) {
            std::cerr << "Skipping line " << line_no << " (bad number): " << line << "\n";
        }
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: extract_targets_from_pcap <metadata.json> <pcap> <input_targets.csv> <output.csv> "
                     "[--fallback-m=0.3] [--max-scans=0]\n";
        return 1;
    }

    const std::string json_path = argv[1];
    const std::string pcap_path = argv[2];
    const std::string targets_in = argv[3];
    const std::string out_path = argv[4];

    double fallback_m = 0.3;
    size_t max_scans = 0;
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--fallback-m=", 0) == 0) {
            try {
                fallback_m = std::stod(arg.substr(std::string("--fallback-m=").size()));
            } catch (...) {
                std::cerr << "Invalid --fallback-m, keeping default 0.3\n";
            }
        } else if (arg.rfind("--max-scans=", 0) == 0) {
            try {
                max_scans = static_cast<size_t>(
                    std::stoul(arg.substr(std::string("--max-scans=").size())));
            } catch (...) {
                std::cerr << "Invalid --max-scans, keeping default 0\n";
            }
        }
    }

    try {
        auto targets = load_targets(targets_in);
        if (targets.empty()) {
            std::cerr << "No valid targets loaded from " << targets_in << "\n";
            return 1;
        }

        PcapDataManipulator manip;
        if (!manip.load_metadata(json_path)) {
            std::cerr << "Failed to load metadata: " << json_path << "\n";
            return 1;
        }
        const int scans = manip.load_pcap(pcap_path);
        if (scans <= 0) {
            std::cerr << "Failed to load PCAP: " << pcap_path << "\n";
            return 1;
        }

        const auto lut = ouster::make_xyz_lut(manip.get_sensor_info(), true);
        std::vector<Hit> hits(targets.size());
        const double r2 = fallback_m * fallback_m;

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
                    const double d2 = dx * dx + dy * dy;
                    if (d2 <= r2 && d2 < hits[ti].dist2) {
                        hits[ti].found = true;
                        hits[ti].x = x;
                        hits[ti].y = y;
                        hits[ti].z = z;
                        hits[ti].dist2 = d2;
                    }
                }
            }
        }

        std::ofstream ofs(out_path, std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "Cannot open output: " << out_path << "\n";
            return 1;
        }
        ofs << "id,x_hit_mm,y_hit_mm,z_hit_mm,found,dist_xy_mm\n";
        ofs << std::fixed << std::setprecision(6);
        const double kToMm = 1000.0;
        for (size_t i = 0; i < targets.size(); ++i) {
            ofs << targets[i].id << ",";
            if (hits[i].found) {
                ofs << hits[i].x * kToMm << ","
                    << hits[i].y * kToMm << ","
                    << hits[i].z * kToMm << ",0,"
                    << std::sqrt(hits[i].dist2) * kToMm << "\n";
            } else {
                ofs << "nan,nan,nan,1,nan\n";
            }
        }
        std::cout << "Wrote snapped targets to " << out_path << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
