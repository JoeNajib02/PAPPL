// Rail-specific profile filtering and denoising.
#pragma once

#include <vector>
#include <Eigen/Dense>

namespace ouster {
namespace rail {

struct RailProfileConfig {
    double bin_size_m = 0.01;      // spacing of samples along rail axis
    int median_window = 5;         // odd window size for median filter
    int sg_window = 11;            // odd window size for Savitzky-Golay smoothing
    int sg_poly = 3;               // polynomial order for Savitzky-Golay (<= sg_window-1)
    double noise_floor_mm = 2.0;   // variations below this are zeroed
    double min_signal_mm = 3.5;    // deformations above this are preserved verbatim
};

struct RailProfileSample {
    double distance_along_rail_m;
    double filtered_vertical_deviation_mm;
};

class RailProfileProcessor {
  public:
    explicit RailProfileProcessor(RailProfileConfig cfg = {});

    // Returns denoised rail profile (distance along rail, vertical deviation in mm).
    std::vector<RailProfileSample> process(const std::vector<Eigen::Vector3d>& points) const;

  private:
    RailProfileConfig cfg_;
};

}  // namespace rail
}  // namespace ouster
