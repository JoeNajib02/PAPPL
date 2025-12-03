/**
 * Repeatability-oriented filtering and metrics helpers for the demos.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ouster/lidar_scan.h"
#include "ouster/types.h"

namespace ouster {
namespace sensor_utils {

struct MetricsOptions {
    size_t stride_rows = 4;
    size_t stride_cols = 8;
    size_t max_scans = 0;  // 0 = all
};

struct RepeatabilityMetrics {
    size_t frames_used = 0;
    size_t samples = 0;
    double mean_range_mm = 0.0;
    double std_range_mm = 0.0;
    double mean_abs_dev_mm = 0.0;
    double frame_mean_std_mm = 0.0;
    double valid_ratio = 0.0;
};

class RepeatabilityFilter {
   public:
    virtual ~RepeatabilityFilter() = default;
    virtual void apply(std::vector<ouster::LidarScan>& scans,
                       const sensor::sensor_info& info) = 0;
};

class RepeatabilityPipeline {
   public:
    explicit RepeatabilityPipeline(sensor::sensor_info info);

    void add_filter(std::unique_ptr<RepeatabilityFilter> filter);
    std::vector<ouster::LidarScan> run(
        const std::vector<ouster::LidarScan>& scans);

   private:
    sensor::sensor_info info_;
    std::vector<std::unique_ptr<RepeatabilityFilter>> filters_;
};

class RepeatabilityAnalyzer {
   public:
    static RepeatabilityMetrics compute_global_range_metrics(
        const std::vector<ouster::LidarScan>& scans,
        const sensor::sensor_info& info,
        const MetricsOptions& opts);
};

class RepeatabilityReportWriter {
   public:
    static bool write_csv(const std::string& path,
                          const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
    static bool write_json(const std::string& path,
                           const std::vector<std::pair<std::string, RepeatabilityMetrics>>& rows);
};

class StatisticalOutlierFilter : public RepeatabilityFilter {
   public:
    StatisticalOutlierFilter(double z_thresh, uint32_t min_range_mm);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double z_thresh_;
    uint32_t min_range_mm_;
};

class ExponentialSmootherFilter : public RepeatabilityFilter {
   public:
    explicit ExponentialSmootherFilter(double alpha);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double alpha_;
};

class PlanaritySmoother : public RepeatabilityFilter {
   public:
    PlanaritySmoother(int radius, uint32_t base_range_mm, double plane_threshold);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int radius_;
    uint32_t base_range_mm_;
    double plane_threshold_;
};

class MedianTemporalFilter : public RepeatabilityFilter {
   public:
    explicit MedianTemporalFilter(int radius);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int radius_;
};

class KalmanRangeFilter : public RepeatabilityFilter {
   public:
    KalmanRangeFilter(double process_noise_mm2, double measurement_noise_mm2);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double process_noise_mm2_;
    double measurement_noise_mm2_;
};

class HuberSmootherFilter : public RepeatabilityFilter {
   public:
    HuberSmootherFilter(int radius, int iterations, double delta_mm,
                        double blend);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int radius_;
    int iterations_;
    double delta_mm_;
    double blend_;
};

}  // namespace sensor_utils
}  // namespace ouster

