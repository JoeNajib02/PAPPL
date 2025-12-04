/**
 * Implementation of the example demo runner classes.
 */

#include "demo_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace ouster;
using namespace ouster::sensor_utils;

namespace {
size_t lidar_height(const sensor::sensor_info& info, const LidarScan& scan) {
    return static_cast<size_t>(scan.h);
}

size_t lidar_width(const sensor::sensor_info& info, const LidarScan& scan) {
    return static_cast<size_t>(scan.w);
}
}  // namespace

DemoContext::DemoContext(std::string pcap_path, std::string json_path)
    : pcap_path_(std::move(pcap_path)), json_path_(std::move(json_path)) {}

bool DemoContext::load() {
    if (!manipulator_.load_metadata(json_path_)) {
        std::cerr << "Failed to load metadata\n";
        return false;
    }
    int num_scans = manipulator_.load_pcap(pcap_path_);
    if (num_scans <= 0) {
        std::cerr << "Failed to load PCAP or no scans found\n";
        return false;
    }

    baseline_scans_.clear();
    baseline_scans_.reserve(manipulator_.num_scans());
    for (const auto& scan_ref : manipulator_.get_all_scans()) {
        baseline_scans_.push_back(scan_ref.get());
    }
    base_stats_ = manipulator_.get_statistics();

    loaded_ = true;
    return true;
}

const sensor::sensor_info& DemoContext::info() const {
    return manipulator_.get_sensor_info();
}

const std::vector<LidarScan>& DemoContext::baseline_scans() const {
    return baseline_scans_;
}

PcapDataManipulator DemoContext::make_isolated_manipulator() const {
    if (!loaded_) {
        throw std::runtime_error("DemoContext not loaded");
    }
    PcapDataManipulator copy(manipulator_);
    (void)base_stats_;
    return copy;
}

ManipulationExamples::ManipulationExamples(const DemoContext& ctx)
    : ctx_(ctx) {}

void ManipulationExamples::example_basic_loading(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 1: Basic Loading and Inspection ===\n"
              << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());
    std::cout << "Successfully loaded " << num_scans << " scans\n"
              << std::endl;

    const auto& info = manipulator.get_sensor_info();
    std::cout << "Sensor Info:\n"
              << "  Serial: " << info.sn << "\n"
              << "  Firmware: " << info.image_rev << "\n"
              << "  Model: " << info.prod_line << "\n"
              << "  Resolution: " << info.format.columns_per_frame << " x "
              << info.format.pixels_per_column << "\n"
              << std::endl;

    const auto& stats = manipulator.get_statistics();
    std::cout << "Data Statistics:\n"
              << "  Total Scans: " << stats.total_scans << "\n"
              << "  Total Packets: " << stats.total_packets << "\n"
              << "  Start Timestamp: " << stats.start_timestamp << "\n"
              << "  End Timestamp: " << stats.end_timestamp << "\n"
              << std::endl;

    auto range_stats = manipulator.compute_range_statistics(8, 8, 2);
    std::cout << "Sampled range stats (stride 8x8, first 2 scans):\n"
              << "  Mean: " << range_stats.mean << " mm\n"
              << "  Std:  " << range_stats.stddev << " mm\n"
              << std::endl;

    if (num_scans > 0) {
        const auto& first_scan = manipulator.get_scan(0);
        std::cout << "First Scan:\n"
                  << "  Frame ID: " << first_scan.frame_id << "\n"
                  << "  Timestamp: " << first_scan.timestamp()(0)
                  << " nanoseconds\n"
                  << "  Dimensions: " << first_scan.w << " x "
                  << first_scan.h << "\n"
                  << std::endl;
    }
}

void ManipulationExamples::example_filtering_scans(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 2: Filtering Scans ===\n" << std::endl;

    int initial_scans = static_cast<int>(manipulator.num_scans());
    std::cout << "Initial number of scans: " << initial_scans << std::endl;

    auto remaining = manipulator.filter_scans(
        [](size_t /*idx*/, LidarScan& scan, const sensor::sensor_info& /*info*/) {
            if (scan.frame_id % 2 == 0) {
                return PcapDataManipulator::ProcessResult::SUCCESS;
            }
            return PcapDataManipulator::ProcessResult::FILTERED_OUT;
        });

    std::cout << "After filtering (even frame IDs): " << remaining << " scans"
              << std::endl;
}

void ManipulationExamples::example_range_signal_filtering(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 3: Range and Signal Filtering ===\n"
              << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());
    std::cout << "Loaded " << num_scans << " scans" << std::endl;

    const auto& info = manipulator.get_sensor_info();
    const auto height =
        static_cast<size_t>(info.format.pixels_per_column);
    const auto width =
        static_cast<size_t>(info.format.columns_per_frame);
    size_t total_measurements =
        static_cast<size_t>(num_scans) * height * width;
    std::cout << "Total measurements before filtering: " << total_measurements
              << std::endl;

    size_t filtered_by_range = manipulator.filter_by_range(5000);
    std::cout << "Measurements filtered by range (< 5m): " << filtered_by_range
              << std::endl;

    size_t filtered_by_signal = manipulator.filter_by_signal(100);
    std::cout << "Measurements filtered by signal (< 100): " << filtered_by_signal
              << std::endl;

    size_t remaining = total_measurements - filtered_by_range - filtered_by_signal;
    std::cout << "Remaining measurements: " << remaining << std::endl;
}

void ManipulationExamples::example_pixel_manipulation(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 4: Pixel-Level Manipulation ===\n" << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());
    if (num_scans == 0) {
        std::cerr << "No scans loaded" << std::endl;
        return;
    }

    auto range = manipulator.get_pixel_value(0, 0, 0, sensor::ChanField::RANGE);
    auto signal =
        manipulator.get_pixel_value(0, 0, 0, sensor::ChanField::SIGNAL);

    std::cout << "First pixel (row=0, col=0) of first scan:\n"
              << "  Range: " << range << " mm\n"
              << "  Signal: " << signal << "\n"
              << std::endl;

    manipulator.set_pixel_value(0, 0, 0, sensor::ChanField::RANGE, 0);
    std::cout << "Invalidated first pixel (set range to 0)" << std::endl;

    size_t invalid_count = 0;
    manipulator.process_pixels(
        [&invalid_count](size_t /*scan_idx*/, size_t row, size_t col,
                         LidarScan& scan, const sensor::sensor_info& /*info*/) {
            try {
                auto range_field =
                    scan.field<uint32_t>(sensor::ChanField::RANGE);
                if (range_field(static_cast<int>(row),
                                static_cast<int>(col)) == 0) {
                    invalid_count++;
                }
            } catch (...) {
            }
            return true;
        });

    std::cout << "Total invalid measurements found: " << invalid_count
              << std::endl;
}

void ManipulationExamples::example_point_cloud_analysis(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 5: Point Cloud Generation ===\n" << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());
    if (num_scans == 0) {
        std::cerr << "No scans loaded" << std::endl;
        return;
    }

    auto point_cloud = manipulator.get_point_cloud(0);
    std::cout << "Point cloud dimensions: " << point_cloud.rows() << " points x "
              << point_cloud.cols() << " dimensions\n"
              << std::endl;

    auto valid_points = manipulator.get_valid_points(0);
    std::cout << "Valid points (range > 0): " << valid_points.size()
              << std::endl;

    if (!valid_points.empty()) {
        double min_x = valid_points[0].x();
        double max_x = valid_points[0].x();
        double min_y = valid_points[0].y();
        double max_y = valid_points[0].y();
        double min_z = valid_points[0].z();
        double max_z = valid_points[0].z();

        for (const auto& point : valid_points) {
            min_x = std::min(min_x, point.x());
            max_x = std::max(max_x, point.x());
            min_y = std::min(min_y, point.y());
            max_y = std::max(max_y, point.y());
            min_z = std::min(min_z, point.z());
            max_z = std::max(max_z, point.z());
        }

        std::cout << "\nPoint cloud statistics:\n"
                  << "  X range: [" << std::fixed << std::setprecision(2)
                  << min_x << ", " << max_x << "] meters\n"
                  << "  Y range: [" << min_y << ", " << max_y
                  << "] meters\n"
                  << "  Z range: [" << min_z << ", " << max_z
                  << "] meters\n"
                  << std::endl;
    }
}

void ManipulationExamples::example_annotation_metadata(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 6: Annotation and Metadata ===\n"
              << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());

    for (size_t i = 0; i < static_cast<size_t>(num_scans); ++i) {
        manipulator.annotate_scan(i, "processed", "true");
        manipulator.annotate_scan(i, "filter_version", "1.0");
        manipulator.annotate_scan(i, "scan_index",
                                  std::to_string(i));
    }

    std::cout << "Added annotations to all scans" << std::endl;

    if (num_scans > 0) {
        const auto& annotations = manipulator.get_all_annotations(0);
        std::cout << "Annotations for scan 0:\n";
        for (const auto& kv : annotations) {
            std::cout << "  " << kv.first << ": " << kv.second << "\n";
        }
        std::cout << std::endl;
    }
}

void ManipulationExamples::example_export_csv(
    PcapDataManipulator& manipulator) {
    std::cout << "\n=== Example 7: Point Counting Demo ===\n" << std::endl;

    int num_scans = static_cast<int>(manipulator.num_scans());
    if (num_scans == 0) {
        std::cerr << "No scans loaded" << std::endl;
        return;
    }

    std::cout << "Counting first 5000 valid points..." << std::endl;
    const auto& info = manipulator.get_sensor_info();
    long point_count = 0;
    const long limit = 5000;
    const auto h = lidar_height(info, manipulator.get_scan(0));
    const auto w = lidar_width(info, manipulator.get_scan(0));

    for (size_t s = 0; s < static_cast<size_t>(num_scans) && point_count < limit; ++s) {
        try {
            const auto& scan = manipulator.get_scan(s);
            auto range_field = scan.field<uint32_t>(sensor::ChanField::RANGE);

            for (size_t row = 0; row < h && point_count < limit; ++row) {
                for (size_t col = 0; col < w && point_count < limit; ++col) {
                    if (range_field(static_cast<int>(row),
                                    static_cast<int>(col)) > 0) {
                        point_count++;
                    }
                }
            }
        } catch (...) {
            // Skip scans with errors
        }
    }

    std::cout << "  \xE2\x9C\x93 Counted " << point_count << " valid points (sample size)"
              << std::endl;
    std::cout << "\nNote: Full point cloud export available via:" << std::endl;
    std::cout << "  manipulator.export_all_points_to_csv(\"output.csv\")"
              << std::endl;
    std::cout << "  manipulator.export_scan_to_csv(scan_idx, \"output.csv\")"
              << std::endl;
    std::cout << "  (Full export can be slow for large datasets; use for"
              << " production data processing)" << std::endl;
}

void ManipulationExamples::run_all() {
    auto manip_a = ctx_.make_isolated_manipulator();
    example_basic_loading(manip_a);

    auto manip_b = ctx_.make_isolated_manipulator();
    example_filtering_scans(manip_b);

    auto manip_c = ctx_.make_isolated_manipulator();
    example_range_signal_filtering(manip_c);

    auto manip_d = ctx_.make_isolated_manipulator();
    example_pixel_manipulation(manip_d);

    auto manip_e = ctx_.make_isolated_manipulator();
    example_point_cloud_analysis(manip_e);

    auto manip_f = ctx_.make_isolated_manipulator();
    example_annotation_metadata(manip_f);

    auto manip_g = ctx_.make_isolated_manipulator();
    example_export_csv(manip_g);
}

bool ManipulationExamples::run_only(const std::string& which) {
    if (which == "basic") {
        auto m = ctx_.make_isolated_manipulator();
        example_basic_loading(m);
        return true;
    }
    if (which == "filter") {
        auto m = ctx_.make_isolated_manipulator();
        example_filtering_scans(m);
        return true;
    }
    if (which == "range") {
        auto m = ctx_.make_isolated_manipulator();
        example_range_signal_filtering(m);
        return true;
    }
    if (which == "pixel") {
        auto m = ctx_.make_isolated_manipulator();
        example_pixel_manipulation(m);
        return true;
    }
    if (which == "cloud") {
        auto m = ctx_.make_isolated_manipulator();
        example_point_cloud_analysis(m);
        return true;
    }
    if (which == "annotate") {
        auto m = ctx_.make_isolated_manipulator();
        example_annotation_metadata(m);
        return true;
    }
    if (which == "export") {
        auto m = ctx_.make_isolated_manipulator();
        example_export_csv(m);
        return true;
    }
    return false;
}

RepeatabilityDemo::RepeatabilityDemo(const DemoContext& ctx) : ctx_(ctx) {}

void RepeatabilityDemo::print_metrics(
    const std::string& label, const RepeatabilityMetrics& m) {
    std::cout << "\n[" << label << "]" << std::endl;
    std::cout << "  frames used:     " << m.frames_used << std::endl;
    std::cout << "  samples:         " << m.samples << std::endl;
    std::cout << "  mean range (mm): " << m.mean_range_mm << std::endl;
    std::cout << "  std range (mm):  " << m.std_range_mm << std::endl;
    std::cout << "  mean abs dev:    " << m.mean_abs_dev_mm << std::endl;
    std::cout << "  frame mean std:  " << m.frame_mean_std_mm << std::endl;
    std::cout << "  valid ratio:     " << m.valid_ratio << std::endl;
}

void RepeatabilityDemo::run() {
    std::cout << "\n=== Repeatability Filters ===\n" << std::endl;
    const auto& info = ctx_.info();
    const auto& scans = ctx_.baseline_scans();

    MetricsOptions metric_opts;
    metric_opts.stride_rows = 4;
    metric_opts.stride_cols = 8;
    metric_opts.max_scans = std::min<size_t>(60, scans.size());  // keep runtime manageable

    auto baseline_metrics =
        RepeatabilityAnalyzer::compute_global_range_metrics(
            scans, info, metric_opts);
    print_metrics("baseline (raw)", baseline_metrics);

    RepeatabilityPipeline staged(info);
    staged.add_filter(std::make_unique<StatisticalOutlierFilter>(2.5, 1500));
    staged.add_filter(std::make_unique<PlanaritySmoother>(1, 150, 0.6));
    staged.add_filter(std::make_unique<NormalGuidedSmoother>(12.0, 0.6));
    auto staged_result = staged.run(scans);
    auto staged_metrics = RepeatabilityAnalyzer::compute_global_range_metrics(
        staged_result, info, metric_opts);
    print_metrics("staged: outlier -> planarity -> normals", staged_metrics);

    RepeatabilityPipeline kalman(info);
    kalman.add_filter(std::make_unique<KalmanRangeFilter>(400.0, 2500.0));
    auto kalman_result = kalman.run(scans);
    auto kalman_metrics = RepeatabilityAnalyzer::compute_global_range_metrics(
        kalman_result, info, metric_opts);
    print_metrics("kalman (per-pixel)", kalman_metrics);

    std::vector<std::pair<std::string, RepeatabilityMetrics>> rows = {
        {"baseline", baseline_metrics},
        {"staged_outlier_planarity_normals", staged_metrics},
        {"kalman", kalman_metrics}};
    RepeatabilityReportWriter::write_csv("repeatability_report.csv", rows);
    RepeatabilityReportWriter::write_json("repeatability_report.json", rows);

    std::cout << "\nDone. Use these metrics to decide which filter chain fits "
                 "your repeatability target."
              << std::endl;
}
