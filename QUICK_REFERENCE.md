# Quick Reference - Standalone Ouster PCAP Manipulator

## 📍 Project Location
```
/Users/michel/MichelFile/pappllocal/ouster-pcap-manipulator/
```

## 🚀 Quick Build & Run

```bash
# Build
cd /Users/michel/MichelFile/pappllocal/ouster-pcap-manipulator
mkdir build && cd build
cmake ..
cmake --build .

# Run
./examples/pcap_manipulation_example /path/to/data.pcap /path/to/metadata.json .
```

## 📦 What's Included

| File | Lines | Purpose |
|------|-------|---------|
| `src/pcap_data_manipulator.h` | ~460 | Public API for PCAP manipulation |
| `src/pcap_data_manipulator.cpp` | ~700 | Implementation using Ouster SDK |
| `examples/pcap_manipulation_example.cpp` | ~380 | 7 runnable examples |
| `CMakeLists.txt` | ~50 | Standalone build config |
| `README.md` | ~200 | Full documentation |

## 🎯 Main Features

- ✅ Load PCAP + JSON metadata
- ✅ Batch packets into complete scans
- ✅ Filter by range, signal, azimuth, altitude
- ✅ Manipulate individual pixels
- ✅ Generate point clouds (XYZ)
- ✅ Annotate scans
- ✅ Export to CSV

## 📖 7 Examples in One Binary

Run `./examples/pcap_manipulation_example` and it executes:

1. **Basic loading** — Load PCAP, print sensor info
2. **Filter scans** — Remove based on criteria
3. **Range/signal filtering** — Invalidate measurements
4. **Pixel manipulation** — Access/modify individual measurements
5. **Point cloud analysis** — Generate XYZ, compute statistics
6. **Annotations** — Add/retrieve custom metadata
7. **Point counting** — Fast demo (first 5000 points)

## 🔧 Build Options

### With system-installed SDK (Homebrew)
```bash
cmake ..
cmake --build .
```

### With custom SDK path
```bash
cmake .. -DCMAKE_PREFIX_PATH=/path/to/sdk/install
cmake --build .
```

### With verbose output
```bash
cmake --build . --verbose
```

## 💡 Key API Usage

```cpp
#include "ouster-pcap-manipulator/pcap_data_manipulator.h"
using namespace ouster::sensor_utils;

// Create manipulator
PcapDataManipulator manip;

// Load data
manip.load_metadata("metadata.json");
manip.load_pcap("data.pcap");

// Filter (5 meters = 5000 mm)
manip.filter_by_range(5000);
manip.filter_by_signal(100);

// Analyze
auto valid_points = manip.get_valid_points(0);
auto point_cloud = manip.get_point_cloud(0);

// Annotate
manip.annotate_scan(0, "processed", "true");

// Export
manip.export_scan_to_csv(0, "output.csv");
```

## ⚙️ Dependencies

- **C++14** compiler (GCC, Clang, MSVC)
- **CMake** 3.10+
- **Ouster SDK** (external, installed separately)
- **Eigen3** (usually installed with SDK)

## 🗂️ File Structure

```
ouster-pcap-manipulator/
├── CMakeLists.txt          ← Start here for build
├── README.md               ← Full documentation
├── INCLUDES_AND_PATHS.md   ← Include path troubleshooting
├── .gitignore              ← Standard C++ excludes
├── src/
│   ├── pcap_data_manipulator.h     ← Public API
│   └── pcap_data_manipulator.cpp   ← Implementation
└── examples/
    └── pcap_manipulation_example.cpp  ← All 7 examples
```

## 🔍 Troubleshooting

| Problem | Solution |
|---------|----------|
| "OusterSDK not found" | Install SDK: `brew install ouster-sdk` or build from source |
| Build fails | Check CMAKE_PREFIX_PATH points to SDK install directory |
| Include errors | Run CMake with `-DCMAKE_PREFIX_PATH=/path/to/sdk/install` |
| Example crashes | Ensure paths to .pcap and .json files are correct |

## 📊 Performance Notes

- **Typical run time** for 1200-scan PCAP: ~79 seconds
- **Memory**: ~1-2 GB for 1200 scans (152k+ points)
- **Point export**: Fast for samples, slow for all points (>1M = hours with CSV)

## 🎓 Learning Path

1. Read `README.md`
2. Build the project
3. Run example with your PCAP/JSON
4. Review 7 examples in `examples/pcap_manipulation_example.cpp`
5. Read `src/pcap_data_manipulator.h` for API details
6. Write your own program using the manipulator

## 🔗 Official Resources

- [Ouster SDK GitHub](https://github.com/ouster-lidar/ouster_example)
- [Ouster Docs](https://docs.ousterlidar.com/)

## ✅ Status

- ✅ Source files copied
- ✅ CMakeLists.txt configured
- ✅ README and documentation created
- ✅ .gitignore added
- ✅ Ready to build and use independently

---

**Next Step**: Run `cmake .. && cmake --build .` to build! 🚀
