# Build Issues and Solution

## Problem
The ouster-sdk has an Eigen compatibility issue with Eigen 3.4.1 (installed via homebrew as eigen@3) in the `image_processing.cpp` file. The visualization code uses `.max().min()` chaining which causes compilation errors.

## Solution
We've created a simplified implementation that works around the ouster-sdk build issues by:

1. Using header-only includes from ouster-sdk where possible
2. Creating a minimal implementation that links against the pcap library directly
3. Avoiding the image_processing.cpp compilation issue

## Building

```bash
cd /Users/michel/MichelFile/pappllocal/ouster-pcap-manipulator
rm -rf build
mkdir build
cd build
cmake .. -DEigen3_DIR=/opt/homebrew/opt/eigen@3/share/eigen3/cmake
make
```

## Run Example

```bash
./build/pcap_manipulation_example \
  "/Users/michel/MichelFile/pappllocal/DataReceived/OS-1-128-SR_122441000353_2048x10_20251002_144257-001.pcap" \
  "/Users/michel/MichelFile/pappllocal/DataReceived/OS-1-128-SR_122441000353_2048x10_20251002_144257.json"
```
