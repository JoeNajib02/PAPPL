/**
 * Unified example runner with a single main entry.
 */

#include <cstdlib>
#include <iostream>

#include "demo_runner.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr
            << "Usage: pcap_manipulation_example <pcap_file> <json_file>"
            << std::endl;
        return EXIT_FAILURE;
    }

    std::string pcap_path = argv[1];
    std::string json_path = argv[2];

    DemoContext ctx(pcap_path, json_path);
    if (!ctx.load()) {
        return EXIT_FAILURE;
    }

    ManipulationExamples manipulation(ctx);
    RepeatabilityDemo repeatability(ctx);

    const bool quick_only = std::getenv("OUSTER_QUICK") != nullptr;

    if (const char* only = std::getenv("OUSTER_ONLY")) {
        std::string which = only;
        if (which == "repeat") {
            repeatability.run();
        } else if (!manipulation.run_only(which)) {
            std::cerr << "Unknown OUSTER_ONLY value: " << which << std::endl;
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (quick_only) {
        manipulation.run_only("basic");
        std::cout << "[QUICK MODE] Skipping subsequent examples.\n";
        return EXIT_SUCCESS;
    }

    manipulation.run_all();
    repeatability.run();

    std::cout << "\n=== All examples completed successfully! ===\n" << std::endl;
    return EXIT_SUCCESS;
}

