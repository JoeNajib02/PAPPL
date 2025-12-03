/**
 * Minimal repeatability helpers used by the example runner.
 */

#include "repeatability_filters.h"
#include "scan_statistics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <vector>

#include <Eigen/Dense>

namespace ouster {
namespace sensor_utils {
namespace {

template <typename T>
T clamp_non_negative(T value) {
    return value < static_cast<T>(0) ? static_cast<T>(0) : value;
}

double median(std::vector<double>& values) {
    if (values.empty()) return 0.0;
    size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double med = values[mid];
    if (values.size() % 2 == 0) {
        std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
        med = 0.5 * (med + values[mid - 1]);
    }
    return med;
}

}  // namespace

RepeatabilityPipeline::RepeatabilityPipeline(sensor::sensor_info info)
    : info_(std::move(info)) {}

void RepeatabilityPipeline::add_filter(
    std::unique_ptr<RepeatabilityFilter> filter) {
    filters_.push_back(std::move(filter));
}

std::vector<ouster::LidarScan> RepeatabilityPipeline::run(
    const std::vector<ouster::LidarScan>& scans) {
    std::vector<ouster::LidarScan> result = scans;
    for (auto& filter : filters_) {
        filter->apply(result, info_);
    }
    return result;
}

RepeatabilityMetrics RepeatabilityAnalyzer::compute_global_range_metrics(
    const std::vector<ouster::LidarScan>& scans,
    const sensor::sensor_info& info,
    const MetricsOptions& opts) {
    RepeatabilityMetrics metrics{};
    if (scans.empty()) return metrics;

    const size_t max_scans =
        opts.max_scans > 0 ? std::min(opts.max_scans, scans.size())
                           : scans.size();
    const size_t stride_r = std::max<size_t>(1, opts.stride_rows);
    const size_t stride_c = std::max<size_t>(1, opts.stride_cols);

    std::vector<double> samples;
    samples.reserve(max_scans * 128);
    std::vector<double> frame_means;
    frame_means.reserve(max_scans);

    size_t total_possible = 0;

    for (size_t s = 0; s < max_scans; ++s) {
        const auto& scan = scans[s];
        auto range_field =
            scan.field<uint32_t>(sensor::ChanField::RANGE);
        double frame_sum = 0.0;
        size_t frame_count = 0;

        for (int r = 0; r < range_field.rows(); r += static_cast<int>(stride_r)) {
            for (int c = 0; c < range_field.cols(); c += static_cast<int>(stride_c)) {
                total_possible++;
                uint32_t val = range_field(r, c);
                if (val == 0) continue;
                frame_sum += static_cast<double>(val);
                frame_count++;
                samples.push_back(static_cast<double>(val));
            }
        }

        if (frame_count > 0) {
            frame_means.push_back(frame_sum / static_cast<double>(frame_count));
        }
    }

    metrics.frames_used = max_scans;
    metrics.samples = samples.size();
    metrics.valid_ratio = total_possible > 0
                              ? static_cast<double>(metrics.samples) /
                                    static_cast<double>(total_possible)
                              : 0.0;

    const double mean = ScanStatistics::mean(samples);
    metrics.mean_range_mm = mean;
    metrics.std_range_mm = ScanStatistics::stddev(samples, mean);
    metrics.mean_abs_dev_mm =
        ScanStatistics::mean_abs_deviation(samples, mean);

    const double frame_mean = ScanStatistics::mean(frame_means);
    metrics.frame_mean_std_mm = ScanStatistics::stddev(frame_means, frame_mean);

    (void)info;  // Currently unused; keep signature stable for expansion.
    return metrics;
}

bool RepeatabilityReportWriter::write_csv(
    const std::string& path,
    const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;

    ofs << "label,frames_used,samples,mean_range_mm,std_range_mm,mean_abs_dev_mm,"
           "frame_mean_std_mm,valid_ratio\n";
    for (const auto& row : rows) {
        const auto& m = row.second;
        ofs << row.first << ","
            << m.frames_used << ","
            << m.samples << ","
            << m.mean_range_mm << ","
            << m.std_range_mm << ","
            << m.mean_abs_dev_mm << ","
            << m.frame_mean_std_mm << ","
            << m.valid_ratio << "\n";
    }
    return true;
}

bool RepeatabilityReportWriter::write_json(
    const std::string& path,
    const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;

    ofs << "[\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& label = rows[i].first;
        const auto& m = rows[i].second;
        ofs << "  {\n";
        ofs << "    \"label\": \"" << label << "\",\n";
        ofs << "    \"frames_used\": " << m.frames_used << ",\n";
        ofs << "    \"samples\": " << m.samples << ",\n";
        ofs << "    \"mean_range_mm\": " << m.mean_range_mm << ",\n";
        ofs << "    \"std_range_mm\": " << m.std_range_mm << ",\n";
        ofs << "    \"mean_abs_dev_mm\": " << m.mean_abs_dev_mm << ",\n";
        ofs << "    \"frame_mean_std_mm\": " << m.frame_mean_std_mm << ",\n";
        ofs << "    \"valid_ratio\": " << m.valid_ratio << "\n";
        ofs << "  }";
        if (i + 1 < rows.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    return true;
}

StatisticalOutlierFilter::StatisticalOutlierFilter(double z_thresh,
                                                   uint32_t min_range_mm)
    : z_thresh_(z_thresh), min_range_mm_(min_range_mm) {}

void StatisticalOutlierFilter::apply(std::vector<ouster::LidarScan>& scans,
                                     const sensor::sensor_info& /*info*/) {
    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        std::vector<double> vals;
        vals.reserve(static_cast<size_t>(range.rows() * range.cols()));
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v > 0) vals.push_back(static_cast<double>(v));
            }
        }

        const double mean = ScanStatistics::mean(vals);
        const double stddev = ScanStatistics::stddev(vals, mean);
        const double z_limit = stddev > 0 ? z_thresh_ * stddev : 0.0;

        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v == 0) continue;
                if (v < min_range_mm_) {
                    range(r, c) = 0;
                    continue;
                }
                double z = stddev > 0 ? std::abs(static_cast<double>(v) - mean)
                                            : 0.0;
                if (z_limit > 0 && z > z_limit) {
                    range(r, c) = 0;
                }
            }
        }
    }
}

ExponentialSmootherFilter::ExponentialSmootherFilter(double alpha)
    : alpha_(alpha) {}

void ExponentialSmootherFilter::apply(std::vector<ouster::LidarScan>& scans,
                                      const sensor::sensor_info& /*info*/) {
    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        std::vector<double> vals;
        vals.reserve(static_cast<size_t>(range.rows() * range.cols()));
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v > 0) vals.push_back(static_cast<double>(v));
            }
        }
        const double mean = ScanStatistics::mean(vals);
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                auto v = range(r, c);
                if (v == 0) continue;
                double smoothed =
                    alpha_ * static_cast<double>(v) +
                    (1.0 - alpha_) * mean;
                range(r, c) = static_cast<uint32_t>(clamp_non_negative(smoothed));
            }
        }
    }
}

PlanaritySmoother::PlanaritySmoother(int radius,
                                     uint32_t base_range_mm,
                                     double plane_threshold)
    : radius_(radius),
      base_range_mm_(base_range_mm),
      plane_threshold_(plane_threshold) {}

void PlanaritySmoother::apply(std::vector<ouster::LidarScan>& scans,
                              const sensor::sensor_info& /*info*/) {
    const double threshold_mm = plane_threshold_ * static_cast<double>(base_range_mm_);
    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        Eigen::Matrix<uint32_t, Eigen::Dynamic, Eigen::Dynamic> original =
            range;
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                const uint32_t center = original(r, c);
                if (center == 0) continue;

                double sum = 0.0;
                size_t count = 0;
                for (int dr = -radius_; dr <= radius_; ++dr) {
                    const int rr = r + dr;
                    if (rr < 0 || rr >= range.rows()) continue;
                    for (int dc = -radius_; dc <= radius_; ++dc) {
                        const int cc = c + dc;
                        if (cc < 0 || cc >= range.cols()) continue;
                        const uint32_t v = original(rr, cc);
                        if (v == 0) continue;
                        sum += static_cast<double>(v);
                        count++;
                    }
                }
                if (count == 0) continue;
                const double avg = sum / static_cast<double>(count);
                if (std::abs(static_cast<double>(center) - avg) > threshold_mm) {
                    range(r, c) = static_cast<uint32_t>(clamp_non_negative(avg));
                }
            }
        }
    }
}

MedianTemporalFilter::MedianTemporalFilter(int radius) : radius_(radius) {}

void MedianTemporalFilter::apply(std::vector<ouster::LidarScan>& scans,
                                 const sensor::sensor_info& /*info*/) {
    if (scans.empty()) return;

    for (size_t s = 0; s < scans.size(); ++s) {
        auto range = scans[s].field<uint32_t>(sensor::ChanField::RANGE);
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                std::vector<double> vals;
                for (int ds = -radius_; ds <= radius_; ++ds) {
                    const int idx = static_cast<int>(s) + ds;
                    if (idx < 0 || idx >= static_cast<int>(scans.size())) continue;
                    const auto& neigh = scans[static_cast<size_t>(idx)]
                                            .field<uint32_t>(sensor::ChanField::RANGE);
                    const uint32_t v =
                        neigh(r, c);
                    if (v > 0) vals.push_back(static_cast<double>(v));
                }
                if (!vals.empty()) {
                    double med = median(vals);
                    range(r, c) =
                        static_cast<uint32_t>(clamp_non_negative(med));
                }
            }
        }
    }
}

KalmanRangeFilter::KalmanRangeFilter(double process_noise_mm2,
                                     double measurement_noise_mm2)
    : process_noise_mm2_(process_noise_mm2),
      measurement_noise_mm2_(measurement_noise_mm2) {}

void KalmanRangeFilter::apply(std::vector<ouster::LidarScan>& scans,
                              const sensor::sensor_info& info) {
    if (scans.empty()) return;
    const size_t h = static_cast<size_t>(scans.front().h);
    const size_t w = static_cast<size_t>(scans.front().w);
    const size_t total = h * w;
    (void)info;

    std::vector<double> state(total, 0.0);
    std::vector<double> cov(total, 0.0);
    std::vector<bool> initialized(total, false);

    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        for (size_t r = 0; r < h; ++r) {
            for (size_t c = 0; c < w; ++c) {
                const size_t idx = r * w + c;
                const uint32_t meas =
                    range(static_cast<int>(r), static_cast<int>(c));
                if (meas == 0) continue;

                if (!initialized[idx]) {
                    state[idx] = static_cast<double>(meas);
                    cov[idx] = measurement_noise_mm2_;
                    initialized[idx] = true;
                } else {
                    cov[idx] += process_noise_mm2_;
                    const double k_gain =
                        cov[idx] / (cov[idx] + measurement_noise_mm2_);
                    state[idx] =
                        state[idx] + k_gain * (static_cast<double>(meas) - state[idx]);
                    cov[idx] = (1.0 - k_gain) * cov[idx];
                }
                range(static_cast<int>(r), static_cast<int>(c)) =
                    static_cast<uint32_t>(clamp_non_negative(state[idx]));
            }
        }
    }
}

HuberSmootherFilter::HuberSmootherFilter(int radius,
                                         int iterations,
                                         double delta_mm,
                                         double blend)
    : radius_(radius),
      iterations_(iterations),
      delta_mm_(delta_mm),
      blend_(blend) {}

void HuberSmootherFilter::apply(std::vector<ouster::LidarScan>& scans,
                                const sensor::sensor_info& /*info*/) {
    for (auto& scan : scans) {
        for (int iter = 0; iter < iterations_; ++iter) {
            auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
            Eigen::Matrix<uint32_t, Eigen::Dynamic, Eigen::Dynamic> original =
                range;

            for (int r = 0; r < range.rows(); ++r) {
                for (int c = 0; c < range.cols(); ++c) {
                    const uint32_t center = original(r, c);
                    if (center == 0) continue;

                    double weighted_sum = 0.0;
                    double weight_total = 0.0;
                    for (int dr = -radius_; dr <= radius_; ++dr) {
                        const int rr = r + dr;
                        if (rr < 0 || rr >= range.rows()) continue;
                        for (int dc = -radius_; dc <= radius_; ++dc) {
                            const int cc = c + dc;
                            if (cc < 0 || cc >= range.cols()) continue;
                            const uint32_t neighbor = original(rr, cc);
                            if (neighbor == 0) continue;

                            const double diff =
                                static_cast<double>(neighbor) -
                                static_cast<double>(center);
                            const double abs_diff = std::abs(diff);
                            const double weight =
                                abs_diff <= delta_mm_
                                    ? 1.0
                                    : delta_mm_ / (abs_diff + 1e-6);
                            weighted_sum += weight * neighbor;
                            weight_total += weight;
                        }
                    }
                    if (weight_total <= 0.0) continue;
                    const double smooth_value = weighted_sum / weight_total;
                    const double blended = blend_ * smooth_value +
                                           (1.0 - blend_) * center;
                    range(r, c) =
                        static_cast<uint32_t>(clamp_non_negative(blended));
                }
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster
