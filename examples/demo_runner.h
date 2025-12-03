/**
 * OOP wrappers for the example flows to keep a single main entrypoint.
 */
#pragma once

#include <string>
#include <vector>

#include "pcap_data_manipulator.h"
#include "repeatability_filters.h"

class DemoContext {
   public:
    DemoContext(std::string pcap_path, std::string json_path);
    bool load();

    const ouster::sensor::sensor_info& info() const;
    const std::vector<ouster::LidarScan>& baseline_scans() const;
    ouster::sensor_utils::PcapDataManipulator make_isolated_manipulator() const;

   private:
    std::string pcap_path_;
    std::string json_path_;
    ouster::sensor_utils::PcapDataManipulator manipulator_;
    std::vector<ouster::LidarScan> baseline_scans_;
    ouster::sensor_utils::PcapDataManipulator::DataStatistics base_stats_{};
    bool loaded_ = false;
};

class ManipulationExamples {
   public:
    explicit ManipulationExamples(const DemoContext& ctx);
    void run_all();
    bool run_only(const std::string& which);

   private:
    const DemoContext& ctx_;

    void example_basic_loading(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_filtering_scans(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_range_signal_filtering(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_pixel_manipulation(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_point_cloud_analysis(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_annotation_metadata(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    void example_export_csv(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
};

class RepeatabilityDemo {
   public:
    explicit RepeatabilityDemo(const DemoContext& ctx);
    void run();

   private:
    const DemoContext& ctx_;
    void print_metrics(const std::string& label,
                       const ouster::sensor_utils::RepeatabilityMetrics& m);
};

