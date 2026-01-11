# Ouster PCAP Manipulator (Standalone)

Single-source documentation that merges all prior Markdown guides into one clear reference.

## Table of Contents
- [Overview](#overview)
- [Status & Deliverables](#status--deliverables)
- [Quick Start (5 minutes)](#quick-start-5-minutes)
- [Prerequisites](#prerequisites)
- [Build](#build)
- [Run the 7 Built-in Examples](#run-the-7-built-in-examples)
- [Use as a Library](#use-as-a-library)
- [Architecture & Separation](#architecture--separation)
- [Include Paths & Troubleshooting](#include-paths--troubleshooting)
- [SDK Bug Fix (reference)](#sdk-bug-fix-reference)
- [Performance & Test Results](#performance--test-results)
- [Project Layout](#project-layout)
- [Completion & Next Steps](#completion--next-steps)
- [Reference Links](#reference-links)

## Overview
- Standalone C++14 project to load, filter, manipulate, and export Ouster lidar PCAP data using the official Ouster SDK as an external dependency.
- Cleanly separated from the official SDK: build via `find_package(OusterSDK)`; no modifications to the SDK are required.
- Provides 7 runnable examples plus a documented public API (`src/pcap_data_manipulator.h`).

## Status & Deliverables
- ✅ Code and docs extracted into `ouster-pcap-manipulator/`
- ✅ Library target: `pcap_data_manipulator`
- ✅ Example target: `pcap_manipulation_example`
- ✅ Doxygen-ready headers (see `docs/doxygen/html/index.html`)
- ✅ Verified on real data: 1,200 scans (~314M measurements), 7/7 examples pass
- Docs merged from: QUICK_START, QUICK_REFERENCE, START_HERE, SOLUTION_EXPLAINED, ARCHITECTURE, INCLUDES_AND_PATHS, COMPLETION_CHECKLIST, VERIFICATION_CHECKLIST, TEST_RESULTS, RAW_SDK_FIX, BUILD_ISSUES, VISUAL_SUMMARY, PROJECT_COMPLETION_SUMMARY, GETTING_STARTED, INDEX.

## Quick Start (5 minutes)
```bash
cd /path/to/ouster-pcap-manipulator
mkdir -p build && cd build
cmake ..                       # Finds OusterSDK if installed
cmake --build .
./examples/pcap_manipulation_example data.pcap metadata.json .
```
- Need a different SDK path? `cmake .. -DCMAKE_PREFIX_PATH=/path/to/sdk/install`
- Use env var `OUSTER_MAX_SCANS` to cap scans when testing.

## Prerequisites
- CMake ≥ 3.10
- C++14 compiler (GCC/Clang/MSVC)
- Ouster SDK installed (Homebrew, package manager, or source build; ensure `OusterSDKConfig.cmake` is discoverable)
- Eigen3 (bundled with SDK installs)
- libpcap (system/packaged)
- Optional: Qt5 if you build the GUI target.

## Build
**System-installed SDK (Homebrew/Linux packages)**
```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

**Custom SDK install or build directory**
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/ouster-sdk/install
cmake --build .
```

**Verbose build**
```bash
cmake --build . --verbose
```

## External data & toolchain folders
To keep the Git history lean we now store the large SNCF datasets and the
`vcpkg` toolchain **outside** of the repository.  By default we expect the
following sibling directories next to the `PAPPL/` checkout:

```
raw_github/
├─ PAPPL/                 # this repository
├─ PAPPL_datasets/        # heavy demo data
│   ├─ demo_sncf/
│   ├─ demo_sncf 2/
│   ├─ demo_sncf alias/
│   └─ demo_sncf_profiles/
└─ PAPPL_toolchains/
    └─ vcpkg/
```

- **Datasets** – Use the helper env var below (or adapt to your own absolute
  paths) when running `MainProgram`:

  ```bash
  export PAPPL_DATASETS_ROOT="$(cd .. && pwd)/PAPPL_datasets"
  ./build/MainProgram \
    "$PAPPL_DATASETS_ROOT/demo_sncf/2025-11-13_11-46-43_ref/build/OS-1-128_992425000327_1024x10_20251112_180355.json" \
    "$PAPPL_DATASETS_ROOT/demo_sncf_profiles/demo_sncf/2025-11-13_12-23-15_5mm_6.5/x_y_zref_zraw.csv" \
    out/output.csv \
    "$PAPPL_DATASETS_ROOT/demo_sncf/2025-11-13_11-46-43_ref/build/OS-1-128_992425000327_1024x10_20251112_180355.pcap" \
    "$PAPPL_DATASETS_ROOT/demo_sncf/2025-11-13_12-23-15_5mm_6.5/build/OS-1-128_992425000327_1024x10_20251112_184028.pcap"
  ```

- **vcpkg** – Keep the toolchain in `../PAPPL_toolchains/vcpkg` (or anywhere you
  prefer) and point CMake to it via `VCPKG_ROOT` / `CMAKE_TOOLCHAIN_FILE`, e.g.:

  ```bash
  export VCPKG_ROOT="$(cd .. && pwd)/PAPPL_toolchains/vcpkg"
  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  cmake --build build
  ```

Feel free to symlink other personal locations into those folders—the important
part is simply keeping the heavy assets outside the Git checkout.

## Run the 7 Built-in Examples
```bash
./examples/pcap_manipulation_example <pcap_file> <json_file> [output_dir]
```
Examples executed in order:
1) Basic loading & sensor info  
2) Filter scans (e.g., even frame IDs)  
3) Range & signal filtering (zero invalid)  
4) Pixel manipulation (get/set)  
5) Point cloud generation + stats  
6) Scan annotations (key/value)  
7) Fast point counting demo  

### MainProgram: comparing on 10 sampled rail points
The `MainProgram` example can now build a set of 10 targets from profile CSVs and evaluate a candidate PCAP against a reference PCAP on those points. Example:

```bash
./build/Debug/MainProgram metadata.json targets.csv out.csv ref.pcap cand.pcap \
  --manual-rails="-4.515 -11.886 -1.892 0.575 32.714 -2.072  -2.975 -11.986 -1.982 1.965 31.644 -1.962" \
  --profiles-dir=./demo_sncf_profiles --points-per-rail=5
```
- `--manual-rails` expects 12 whitespace-separated numbers: the two endpoints (x,y,z) for each rail.
- `--profiles-dir` is searched for a subfolder containing `rail_1_points.csv` and `rail_2_points.csv`.
- `--points-per-rail` controls how many points are sampled along each rail (default 5 => 10 points).


## Use as a Library
```cpp
#include "ouster-pcap-manipulator/pcap_data_manipulator.h"
using namespace ouster::sensor_utils;

PcapDataManipulator manip;
manip.load_metadata("metadata.json");
manip.load_pcap("data.pcap");
manip.filter_by_range(5000);   // mm
manip.filter_by_signal(100);   // digital units
auto cloud  = manip.get_point_cloud(0);
auto points = manip.get_valid_points(0);
manip.export_scan_to_csv(0, "output.csv", true, true);
```

## Architecture & Separation
- **Before:** Custom code embedded inside the official SDK (ouster_client + examples), causing conflicts when updating the SDK.
- **After:** Two independent trees:
  - Official SDK (clean, unmodified, upgrade anytime)
  - `ouster-pcap-manipulator` (your code), linked via `find_package(OusterSDK)`.
- Build glue (key snippet):
```cmake
find_package(OusterSDK REQUIRED)
add_library(pcap_data_manipulator src/pcap_data_manipulator.cpp)
target_link_libraries(pcap_data_manipulator
    PUBLIC OusterSDK::ouster_client OusterSDK::ouster_pcap)
```

## Include Paths & Troubleshooting
- **"OusterSDK not found"** → set `CMAKE_PREFIX_PATH` to the SDK install or build tree.
- **"types.h / optional.hpp not found"** → install the SDK properly, or install it to a prefix and point `CMAKE_PREFIX_PATH` there.
- **Link errors to pcap** → install libpcap (`brew install libpcap` or `sudo apt-get install libpcap-dev`).
- **Clean rebuild**:
```bash
cd build
rm -rf *
cmake .. [-DCMAKE_PREFIX_PATH=...]
cmake --build . --verbose
```

## SDK Bug Fix (reference)
- Upstream issue observed in `ouster_client/src/image_processing.cpp` (Eigen template mismatch):
```cpp
// Broken
key_eigen = key_eigen.max(0.0).min(1.0);
// Fixed
key_eigen = key_eigen.max(typename std::decay<decltype(key_eigen)>::type::Scalar(0))
                      .min(typename std::decay<decltype(key_eigen)>::type::Scalar(1));
```
- This fix is only needed if you rebuild the SDK from raw source where the bug exists; the standalone project does not require SDK modifications.

## Performance & Test Results
- Dataset: 1,200 scans, 2048x128, ~314M measurements.
- Total example run: ~70 seconds (I/O-bound on PCAP read).
- Valid points in sample scan: ~152k of 262k.
- Filtering example: ~57% points dropped at 5 m range threshold.
- Memory footprint: ~1–2 GB for the dataset.

## Project Layout
```
ouster-pcap-manipulator/
├─ CMakeLists.txt              # Standalone build config
├─ README.md                   # This merged document
├─ src/
│  ├─ pcap_data_manipulator.h  # Public API (Doxygen)
│  └─ pcap_data_manipulator.cpp
├─ examples/
│  └─ pcap_manipulation_example.cpp  # 7 demos in one main
├─ gui/                        # Optional Qt GUI
└─ docs/doxygen/html/index.html # Generated API docs
```

## Completion & Next Steps
- Completed: code extraction, build config, docs merge, Doxygen generation, real-data verification, checklists.
- Optional next steps:
  - Publish to your VCS (keep SDK separate).
  - Add binary export formats (PCD/PLY/LAZ) for faster output.
  - Upstream the Eigen template fix to the SDK if still needed.
  - Add more examples (custom filters, repeatability reporting).

## Reference Links
- Official SDK: https://github.com/ouster-lidar/ouster_example
- Generated docs: `docs/doxygen/html/index.html`
- Source API reference: `src/pcap_data_manipulator.h`

## PAPPL Reproduction Guide (Project V5)

Use the following commands to rebuild the project and reproduce the full analysis results (CSV reports + Plots + Statistics).

### 1. Build the Project
```bash
# Clean build directory
rm -rf build && mkdir build
cd build

# Configure and Compile (Release mode for performance)
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --parallel 4

# Return to root
cd ..
```

### 2. Run the Repeatability Analysis
This script executes `MainProgram` on all datasets (Ref vs 5mm, ini1, 20mm, ini2) using the *Normal Guided* and *Kalman* filters.
```bash
chmod +x run_usb_analysis.sh
./run_usb_analysis.sh
```
*Note: This generates the `output_*.csv` files.*

### 3. Generate Visualizations
Create PNG plots comparing the different filters.
```bash
python3 plot_results.py
```

### 4. Generate Statistical Report
Calculate the quantitative metrics (Mean Bias, StdDev, RMS) to identify the best filter.
```bash
python3 analyze_report.py
```
*Look for "🏆 BEST FILTER" in the output.*
