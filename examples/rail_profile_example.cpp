#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "rail_profile_processor.h"

using ouster::rail::RailProfileConfig;
using ouster::rail::RailProfileProcessor;
using ouster::rail::RailProfileSample;

namespace {

std::vector<Eigen::Vector3d> load_xyz(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open input file: " + path);
    }
    std::vector<Eigen::Vector3d> pts;
    std::string line;
    pts.reserve(10000);
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);
        double x, y, z;
        if (!(iss >> x >> y >> z)) continue;
        pts.emplace_back(x, y, z);
    }
    return pts;
}

void write_csv(const std::string& path, const std::vector<RailProfileSample>& samples) {
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open output file: " + path);
    }
    out << "distance_along_rail_m,filtered_vertical_deviation_mm\n";
    for (const auto& s : samples) {
        out << s.distance_along_rail_m << "," << s.filtered_vertical_deviation_mm << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: rail_profile_example <input_xyz_or_csv> <output_csv>\n";
        return 1;
    }

    const std::string input_path = argv[1];
    const std::string output_path = argv[2];

    try {
        auto pts = load_xyz(input_path);
        if (pts.empty()) {
            throw std::runtime_error("Input file contains no valid points");
        }

        RailProfileConfig cfg;
        // Defaults tuned for mm-level noise suppression; override here if needed.
        RailProfileProcessor proc(cfg);
        auto result = proc.process(pts);
        write_csv(output_path, result);

        std::cout << "Wrote " << result.size() << " samples to " << output_path << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
