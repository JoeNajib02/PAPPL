/**
 * Spatial hole filling implementation.
 */

#include "repeatability/hole_filling_filter.h"

#include <vector>
#include <numeric>

namespace ouster {
namespace sensor_utils {

HoleFillingFilter::HoleFillingFilter(int kernel_size, int min_valid_neighbors)
    : kernel_size_(kernel_size), min_valid_neighbors_(min_valid_neighbors) {
    if (kernel_size_ % 2 == 0) kernel_size_++; // Ensure odd
}

void HoleFillingFilter::apply(std::vector<ouster::LidarScan>& scans,
                              const sensor::sensor_info& info) {
    (void)info;
    const int radius = kernel_size_ / 2;

    for (auto& scan : scans) {
        // We need a copy of the original range to avoid propagating filled values
        // within the same pass (standard convolution practice).
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        Eigen::Matrix<uint32_t, Eigen::Dynamic, Eigen::Dynamic> original = range;
        
        const int rows = static_cast<int>(range.rows());
        const int cols = static_cast<int>(range.cols());

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                // If the pixel is already valid, skip it.
                if (original(r, c) != 0) continue;

                // Collect valid neighbors
                double sum = 0.0;
                int count = 0;

                for (int dr = -radius; dr <= radius; ++dr) {
                    const int nr = r + dr;
                    if (nr < 0 || nr >= rows) continue;

                    for (int dc = -radius; dc <= radius; ++dc) {
                        const int nc = c + dc;
                        if (nc < 0 || nc >= cols) continue;
                        
                        const uint32_t val = original(nr, nc);
                        if (val != 0) {
                            sum += static_cast<double>(val);
                            count++;
                        }
                    }
                }

                if (count >= min_valid_neighbors_) {
                    // Compute mean
                    double mean = sum / static_cast<double>(count);
                    range(r, c) = static_cast<uint32_t>(mean);
                }
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster


