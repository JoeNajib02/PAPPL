/**
 * @file compute_rail_targets.cpp
 * @brief Generate 10 evenly spaced targets (3 m apart) on each of two rails
 *        defined by user-supplied endpoints in 3D. Outputs a CSV:
 *        id,x,y,z
 */

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct Vec3 {
    double x;
    double y;
    double z;
};

struct Target {
    std::string id;
    double x;
    double y;
    double z;
};

std::vector<Target> interpolate(const std::string& prefix,
                                const Vec3& p0,
                                const Vec3& p1,
                                double step_m = 3.0,
                                int count = 10) {
    std::vector<Target> out;
    const double dx = p1.x - p0.x;
    const double dy = p1.y - p0.y;
    const double dz = p1.z - p0.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= 1e-9) return out;
    const double ux = dx / dist;
    const double uy = dy / dist;
    const double uz = dz / dist;
    for (int i = 1; i <= count; ++i) {
        const double s = step_m * static_cast<double>(i);
        Target t;
        t.id = prefix + "_" + std::to_string(i * 3) + "m";
        t.x = p0.x + ux * s;
        t.y = p0.y + uy * s;
        t.z = p0.z + uz * s;
        out.push_back(t);
    }
    return out;
}

bool write_csv(const std::string& path, const std::vector<Target>& targets) {
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << "id,x,y,z\n";
    ofs << std::fixed << std::setprecision(4);
    for (const auto& t : targets) {
        ofs << t.id << "," << t.x << "," << t.y << "," << t.z << "\n";
    }
    return true;
}

int main(int argc, char* argv[]) {
    const std::string out_path =
        (argc >= 2) ? argv[1] : "targets_rails_10pts.csv";

    // User-supplied endpoints (MANUAL_RAILS) for both rails.
    const Vec3 rail1_p0{-4.515, -11.886, -1.892};
    const Vec3 rail1_p1{0.575, 32.714, -2.072};
    const Vec3 rail2_p0{-2.975, -11.986, -1.982};
    const Vec3 rail2_p1{1.965, 31.644, -1.962};

    std::vector<Target> targets;
    auto r1 = interpolate("R1", rail1_p0, rail1_p1);
    auto r2 = interpolate("R2", rail2_p0, rail2_p1);
    targets.insert(targets.end(), r1.begin(), r1.end());
    targets.insert(targets.end(), r2.begin(), r2.end());

    if (targets.empty()) {
        std::cerr << "No targets generated (check endpoints)\n";
        return 1;
    }

    if (!write_csv(out_path, targets)) {
        std::cerr << "Failed to write: " << out_path << "\n";
        return 1;
    }

    std::cout << "Wrote " << targets.size() << " targets to " << out_path << "\n";
    return 0;
}
