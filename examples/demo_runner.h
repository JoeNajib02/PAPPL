/**
 * OOP wrappers for the example flows to keep a single main entrypoint.
 */
#pragma once

#include <string>
#include <vector>

#include "pcap_data_manipulator.h"
#include "repeatability_filters.h"

/**
 * @brief Shared context for the example flows (metadata + baseline scans).
 *
 * Responsible for loading the JSON/PCAP pair once, caching baseline scans, and
 * handing out deep-copied manipulators so individual demos can mutate data
 * without affecting one another. Provides a single source of truth for sensor
 * metadata across examples.
 */
class DemoContext {
   public:
    /**
     * @brief Construct a context with paths to the PCAP and JSON files.
     * @param pcap_path Path to the recorded PCAP file.
     * @param json_path Path to the matching sensor metadata JSON.
     */
    DemoContext(std::string pcap_path, std::string json_path);
    /** @brief Load metadata and decode scans; returns false on failure. */
    bool load();

    /** @brief Accessor for parsed sensor metadata. */
    const ouster::sensor::sensor_info& info() const;
    /** @brief Baseline scans decoded from disk. */
    const std::vector<ouster::LidarScan>& baseline_scans() const;
    /** @brief Create a deep-copied manipulator for a single demo run. */
    ouster::sensor_utils::PcapDataManipulator make_isolated_manipulator() const;

   private:
    std::string pcap_path_;  ///< Path to the PCAP file used across examples.
    std::string json_path_;  ///< Path to the metadata JSON used across examples.
    ouster::sensor_utils::PcapDataManipulator manipulator_; ///< Shared manipulator that holds loaded scans.
    std::vector<ouster::LidarScan> baseline_scans_; ///< Cached copy of scans for repeatable runs.
    ouster::sensor_utils::PcapDataManipulator::DataStatistics base_stats_{}; ///< Statistics captured on initial load.
    bool loaded_ = false; ///< True once metadata and PCAP have been successfully loaded.
};

/**
 * @brief Container for the manipulation-focused example routines.
 *
 * Each example method demonstrates a focused capability of
 * `PcapDataManipulator` (loading, filtering, pixel edits, point cloud export).
 * Methods are split so they can be executed independently via run_only() or
 * together via run_all().
 */
class ManipulationExamples {
   public:
    /** @brief Bind to shared demo context. */
    explicit ManipulationExamples(const DemoContext& ctx);
    /** @brief Run the full suite of manipulation examples sequentially. */
    void run_all();
    /**
     * @brief Run a single named example by key (basic, filter, range, pixel,
     * cloud, annotate, export).
     * @return True if the key was recognized and executed.
     */
    bool run_only(const std::string& which);

   private:
    const DemoContext& ctx_;

    /** @brief Demonstrate loading, metadata inspection, and stats. */
    void example_basic_loading(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Show scan-level filtering via a user callback. */
    void example_filtering_scans(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Demonstrate range/signal threshold filtering. */
    void example_range_signal_filtering(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Demonstrate per-pixel read/write accessors. */
    void example_pixel_manipulation(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Demonstrate point-cloud conversion and simple stats. */
    void example_point_cloud_analysis(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Demonstrate annotation helpers on scans. */
    void example_annotation_metadata(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
    /** @brief Demonstrate a toy CSV export/counting routine. */
    void example_export_csv(
        ouster::sensor_utils::PcapDataManipulator& manipulator);
};

/**
 * @brief Demo harness for the repeatability pipeline and metrics.
 *
 * Applies a set of repeatability filters to the baseline scans and prints
 * metrics for quick inspection.
 */
class RepeatabilityDemo {
   public:
    /** @brief Bind to shared demo context. */
    explicit RepeatabilityDemo(const DemoContext& ctx);
    /** @brief Run the repeatability pipeline and print metrics. */
    void run();

   private:
    const DemoContext& ctx_;
    /** @brief Helper to print metrics with a label. */
    void print_metrics(const std::string& label,
                       const ouster::sensor_utils::RepeatabilityMetrics& m);
};
