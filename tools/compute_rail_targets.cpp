#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

struct Vec3 {
    double x;
    double y;
    double z;
};

Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

double norm(const Vec3& p) {
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

// Uniformly sample t in [0, 1] and pick the point whose distance to the origin is
// closest to the target distance.
Vec3 find_point_at_distance(const Vec3& p1, const Vec3& p2, double target_dist, double& best_dist,
                            double& best_error) {
    const int steps = 20000;
    best_error = std::numeric_limits<double>::max();
    best_dist = 0.0;
    Vec3 best_point = p1;

    for (int i = 0; i <= steps; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(steps);
        Vec3 pt = lerp(p1, p2, t);
        double d = norm(pt);
        double error = std::abs(d - target_dist);
        if (error < best_error) {
            best_error = error;
            best_dist = d;
            best_point = pt;
        }
    }
    return best_point;
}

struct TargetRecord {
    std::string id;
    Vec3 point;
    double target_dist;
    double actual_dist;
    double error;
};

std::string format_distance_label(double meters) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << meters;
    return oss.str();
}

int main() {
    // Rail endpoints in sensor frame (meters).
    const Vec3 P1_1{-4.515, -11.886, -1.892};
    const Vec3 P2_1{0.575, 32.714, -2.072};
    const Vec3 P1_2{-2.975, -11.986, -1.982};
    const Vec3 P2_2{1.965, 31.644, -1.962};

    std::vector<std::pair<std::string, std::pair<Vec3, Vec3>>> rails = {
        {"R1", {P1_1, P2_1}},
        {"R2", {P1_2, P2_2}},
    };

    // Target distances from the sensor (meters).
    std::vector<double> distances = {3.0, 6.0, 9.0, 12.0, 15.0, 18.0, 21.0, 24.0, 27.0, 30.0};

    std::vector<TargetRecord> targets;
    targets.reserve(rails.size() * distances.size());

    // Compute best points for each rail and distance.
    for (const auto& rail : rails) {
        const std::string& prefix = rail.first;
        const Vec3& p1 = rail.second.first;
        const Vec3& p2 = rail.second.second;

        for (double d_target : distances) {
            double best_dist = 0.0;
            double best_error = 0.0;
            Vec3 pt = find_point_at_distance(p1, p2, d_target, best_dist, best_error);

            std::ostringstream id;
            id << prefix << "_" << format_distance_label(d_target) << "m";

            targets.push_back({id.str(), pt, d_target, best_dist, best_error});
        }
    }

    std::ofstream full_csv("targets_rails_10pts_full.csv");
    if (!full_csv) {
        std::cerr << "Failed to open targets_rails_10pts_full.csv for writing.\n";
        return 1;
    }
    full_csv << "id,x_m,y_m,z_m,target_dist_m,distance_error_m\n";
    full_csv << std::fixed << std::setprecision(6);
    for (const auto& t : targets) {
        full_csv << t.id << "," << t.point.x << "," << t.point.y << "," << t.point.z << ","
                 << t.target_dist << "," << t.error << "\n";
    }

    std::ofstream minimal_csv("targets_rails_10pts.csv");
    if (!minimal_csv) {
        std::cerr << "Failed to open targets_rails_10pts.csv for writing.\n";
        return 1;
    }
    minimal_csv << "id,x,y\n";
    minimal_csv << std::fixed << std::setprecision(6);
    for (const auto& t : targets) {
        minimal_csv << t.id << "," << t.point.x << "," << t.point.y << "\n";
    }

    std::cout << std::fixed << std::setprecision(3);
    for (const auto& t : targets) {
        std::cout << t.id << ": d_actual = " << t.actual_dist << " m, error = " << t.error << " m\n";
    }

    return 0;
}
