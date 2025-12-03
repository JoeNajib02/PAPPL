# Ouster PCAP Data Manipulator

A standalone C++ library for loading, parsing, filtering, and manipulating Ouster lidar PCAP files and JSON metadata. This library uses the official **Ouster SDK** as an external dependency.

## Features

- Load PCAP files and JSON metadata
- Batch lidar packets into complete scans
- Filter measurements by range, signal, azimuth, and altitude
- Manipulate individual pixel values
- Generate and analyze point clouds
- Annotate scans with metadata
- Export scans to CSV

## Prerequisites

1. **Ouster SDK** (installed or available)
   - Option A: Install system-wide (e.g., `brew install ouster-sdk` on macOS)
   - Option B: Use `vcpkg` or another package manager
   - Option C: Build from source and set `CMAKE_PREFIX_PATH`

2. **CMake** >= 3.10
3. **C++14 compatible compiler** (GCC, Clang, MSVC)
4. **Eigen3**

## Build Instructions

### Option 1: With system-installed Ouster SDK

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Option 2: With custom Ouster SDK path

If you built Ouster SDK from source:

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="/path/to/ouster-sdk/build"
cmake --build .
```

### Option 3: With Homebrew (macOS)

```bash
# Install Ouster SDK first
brew install ouster-sdk

mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

### As a Library

```cpp
#include "ouster-pcap-manipulator/pcap_data_manipulator.h"

using namespace ouster::sensor_utils;

PcapDataManipulator manipulator;

// Load metadata
manipulator.load_metadata("metadata.json");

// Load PCAP
int num_scans = manipulator.load_pcap("data.pcap");

// Filter measurements
manipulator.filter_by_range(5000);  // Remove points < 5m

// Get point cloud from first scan
auto points = manipulator.get_valid_points(0);

// Export to CSV
manipulator.export_scan_to_csv(0, "output.csv", true, true);
```

### Running the Example

```bash
./pcap_manipulation_example <pcap_file> <json_file> [output_dir]
```

Example:

```bash
./pcap_manipulation_example data.pcap metadata.json .
```

## Documentation

See [examples/pcap_manipulation_example.cpp](examples/pcap_manipulation_example.cpp) for comprehensive usage examples:

- Example 1: Load and inspect metadata
- Example 2: Filter scans by criteria
- Example 3: Range and signal filtering
- Example 4: Pixel-level manipulation
- Example 5: Point cloud generation and analysis
- Example 6: Annotate scans
- Example 7: Fast point counting demo

## Project Structure

```
ouster-pcap-manipulator/
├── CMakeLists.txt                  # Standalone build configuration
├── README.md                       # This file
├── .gitignore
├── src/
│   ├── pcap_data_manipulator.h     # Public API header
│   └── pcap_data_manipulator.cpp   # Implementation
└── examples/
    └── pcap_manipulation_example.cpp  # Runnable examples
```

## Modifications to Ouster SDK

This project is designed to work with an **unmodified** official Ouster SDK. However, note:

- **No modifications required** for the core manipulator functionality
- The example code uses only public SDK APIs (no internal changes needed)

### (For reference) Changes made during development

If you cloned from the modified repo and see differences:

1. **types.h** — Include path for optional-lite (cosmetic, no functional change)
2. **image_processing.cpp** (line 133) — `static_cast<T>` for Eigen compatibility (may be fixed in newer SDK versions)
3. **ouster_client/CMakeLists.txt** — Removed our code; now uses external dependency model

These changes are **NOT needed** when using this standalone project.

## Building Against a Fresh Ouster SDK Clone

If you have a fresh clone of the official Ouster SDK:

```bash
cd /path/to/ouster-sdk
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install . --prefix /tmp/ouster-sdk-install  # Optional

# Then build this project:
cd /path/to/ouster-pcap-manipulator
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=/tmp/ouster-sdk-install
cmake --build .
```

## Performance Notes

- **Full point cloud export** (`export_all_points_to_csv`) can be slow for large datasets (1M+ points).
  - For demos, use `get_valid_points()` with sampling
  - For production, consider binary formats (PCD/PLY)
- **Typical run time** for a 1200-scan dataset: ~80 seconds (limited by PCAP read I/O, not manipulation)

## License

This project is provided as-is. The Ouster SDK is licensed separately; refer to the official repository for details.

## Support

For issues with the Ouster SDK itself, see: https://github.com/ouster-lidar/ouster_example

For issues with this manipulator, check the example code and SDK API documentation.
