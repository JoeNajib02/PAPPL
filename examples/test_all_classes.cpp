/**
 * Lightweight entrypoint to exercise the manipulator, filters, and statistics.
 */

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "pcap_data_manipulator.h"
#include "repeatability_filters.h"

using namespace ouster;
using namespace ouster::sensor_utils;

namespace {
void print_metrics(const std::string& label,
                   const RepeatabilityMetrics& m) {
    std::cout << "\n[" << label << "]\n"
              << "  frames used:     " << m.frames_used << "\n"
              << "  samples:         " << m.samples << "\n"
              << "  mean range (mm): " << m.mean_range_mm << "\n"
              << "  std range (mm):  " << m.std_range_mm << "\n"
              << "  mean abs dev:    " << m.mean_abs_dev_mm << "\n"
              << "  frame mean std:  " << m.frame_mean_std_mm << "\n"
              << "  valid ratio:     " << m.valid_ratio << "\n";
}
}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: test_all_classes <pcap_file> <json_file>\n";
        return EXIT_FAILURE;
    }

    std::string pcap_path = argv[1];
    std::string json_path = argv[2];

    PcapDataManipulator manip;
    if (!manip.load_metadata(json_path)) {
        std::cerr << "Failed to load metadata\n";
        return EXIT_FAILURE;
    }
    if (manip.load_pcap(pcap_path) <= 0) {
        std::cerr << "Failed to load PCAP\n";
        return EXIT_FAILURE;
    }

    std::cout << "Loaded " << manip.num_scans() << " scans from " << pcap_path
              << "\n";

    auto stats_sample = manip.compute_range_statistics(8, 8, 2);
    std::cout << "Sampled range stats (stride 8x8, first 2 scans):\n"
              << "  mean=" << stats_sample.mean << " mm, std="
              << stats_sample.stddev << " mm, samples=" << stats_sample.samples
              << "\n";

    // Copy to preserve baseline for other checks.
    PcapDataManipulator filtered = manip;
    const size_t removed_range = filtered.filter_by_range(5000);
    const size_t removed_signal = filtered.filter_by_signal(100);
    std::cout << "Filter by range removed " << removed_range
              << " measurements; by signal removed " << removed_signal << "\n";

    // Collect baseline scans for metrics.
    std::vector<LidarScan> baseline_scans;
    baseline_scans.reserve(manip.num_scans());
    for (const auto& ref : manip.get_all_scans()) baseline_scans.push_back(ref);

    const auto& info = manip.get_sensor_info();
    MetricsOptions metric_opts;
    metric_opts.stride_rows = 4;
    metric_opts.stride_cols = 8;
    metric_opts.max_scans = std::min<size_t>(60, baseline_scans.size());

    auto baseline_metrics =
        RepeatabilityAnalyzer::compute_global_range_metrics(
            baseline_scans, info, metric_opts);
    print_metrics("baseline", baseline_metrics);

    RepeatabilityPipeline pipeline(info);
    pipeline.add_filter(std::make_unique<StatisticalOutlierFilter>(2.5, 1500));
    pipeline.add_filter(std::make_unique<ExponentialSmootherFilter>(0.35));
    pipeline.add_filter(std::make_unique<PlanaritySmoother>(1, 150, 0.6));
    auto filtered_scans = pipeline.run(baseline_scans);
    auto filtered_metrics =
        RepeatabilityAnalyzer::compute_global_range_metrics(
            filtered_scans, info, metric_opts);
    print_metrics("pipeline (outlier->ema->planarity)", filtered_metrics);

    RepeatabilityPipeline kalman(info);
    kalman.add_filter(std::make_unique<KalmanRangeFilter>(400.0, 2500.0));
    auto kalman_scans = kalman.run(baseline_scans);
    auto kalman_metrics =
        RepeatabilityAnalyzer::compute_global_range_metrics(
            kalman_scans, info, metric_opts);
    print_metrics("kalman", kalman_metrics);

    std::cout << "\nDone.\n";
    return EXIT_SUCCESS;
}
