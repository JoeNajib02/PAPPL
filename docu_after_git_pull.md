## One-tap build and run (macOS & Windows)

1) Get vcpkg once (installs Qt, libpcap, libtins, Eigen):
   - macOS/Linux: `./tools/setup_vcpkg.sh`
   - Windows PowerShell: `.\tools\setup_vcpkg.ps1`

2) Configure + build with a preset (auto-fetches Ouster SDK to `external/ouster-sdk` if missing):
   - macOS arm64: `cmake --preset mac-release-arm64 && cmake --build --preset mac-release-arm64`
   - macOS x64:   `cmake --preset mac-release-x64 && cmake --build --preset mac-release-x64`
   - Windows:     `cmake --preset win-release && cmake --build --preset win-release --config Release`

3) Run the CLI on any PCAP + JSON:
   - macOS: `./build/mac-release-arm64/pcap_manipulation_example <pcap> <json> [outdir]`
   - Windows: `.\build\win-release\Release\pcap_manipulation_example.exe <pcap> <json> [outdir]`

4) Optional packages (cpack targets):
   - macOS: `cmake --build --preset mac-release-arm64-package`
   - Windows: `cmake --build --preset win-release-package --config Release`

Notes:
- Qt5/Qt6 are both supported; vcpkg defaults to Qt6 via `qtbase/qttools`.
- If you already have the Ouster SDK locally, drop it in `external/ouster-sdk` (or set `-DOUSTER_SDK_SOURCE=/path/...`) and the build will use it instead of fetching.
- Windows needs the Npcap runtime driver installed (vcpkg pulls headers/libs; the driver installer is at https://npcap.com).

## Rail profile denoising pipeline (mm-level, rail-specific)

What it does:
- Loads a raw XYZ/CSV point cloud, estimates the rail axis with PCA, bins along the rail, removes global slope, suppresses <2 mm jitter, and preserves >3–4 mm deformations.
- Outputs CSV with `distance_along_rail_m` and `filtered_vertical_deviation_mm`.

How to run the example CLI:
```bash
# macOS
cmake --preset mac-release-arm64 && cmake --build --preset mac-release-arm64
./build/mac-release-arm64/rail_profile_example input.xyz output.csv

# Windows (PowerShell)
cmake --preset win-release && cmake --build --preset win-release --config Release
.\build\win-release\Release\rail_profile_example.exe input.xyz output.csv
```

Input format:
- Plain text with `x y z` (space- or comma-separated), meters, one point per line; lines starting with `#` are ignored.

Algorithm highlights:
- PCA to get rail axis (stable sign), gravity-up for vertical deviations.
- Binning at 1 cm, bin-wise median height to resist outliers/point sparsity.
- Linear trend removal to avoid swallowing local bumps into global alignment.
- Median filter + Savitzky–Golay (window 11, poly 3) for high-frequency noise removal while keeping mm-scale bumps.
- Soft threshold: <2 mm → zeroed; >3.5 mm preserved.

Tuning knobs (see `RailProfileConfig` in `src/rail_profile_processor.h`):
- `bin_size_m` (default 0.01), `median_window`, `sg_window`, `sg_poly`, `noise_floor_mm`, `min_signal_mm`.

first, i put the ouster-sdk next to the PAPPL directory.
i then moved to my branch mich
then i built it: cd /Users/michel/MichelFile/pappllocal/raw2_2/ouster-pcap-manipulatorcmake -S . -B build \  -DOUSTER_SDK_SOURCE="/Users/michel/MichelFile/pappllocal/raw_github/ouster-sdk" \
  -DQt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5 \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/eigen@3;/opt/homebrew" \
  -DBUILD_VIZ=OFF -DBUILD_MAPPING=OFF
cmake --build build -j4

and then i ran the build:
michel@Pico PAPPL % ./build/pcap_manipulation_example /Users/michel/MichelFile/pappllocal/DataReceived/OS-1-128-SR_122441000353_2048x10_20251002_144257-001.pcap /Users/michel/MichelFile/pappllocal/DataReceived/OS-1-128-SR_122441000353_2048x10_20251002_144257.json

it works!


CMake now auto-fetches Ouster SDK (tag 64a6cbe6dff1eea0cb5a79d63b79acf11b68b16d) into external/ouster-sdk when no local copy is present; Qt linking now handles Qt5/Qt6, CMake min bumped to 3.14, and CPack targets enabled (CMakeLists.txt).
Added vcpkg manifest for all deps (Qt, libpcap/npcap, libtins, Eigen) so installs are automatic (vcpkg.json).
Added cross-platform CMake presets for one-tap configure/build/package on macOS and Windows (CMakePresets.json).
Added helper scripts to fetch/bootstrap vcpkg (tools/setup_vcpkg.sh, tools/setup_vcpkg.ps1).
Documented the new workflow and commands (docu_after_git_pull.md).
One-tap build/run (after git clone):

Install deps via vcpkg: ./tools/setup_vcpkg.sh (macOS/Linux) or .\tools\setup_vcpkg.ps1 (Windows).
Configure+build:
macOS arm64: cmake --preset mac-release-arm64 && cmake --build --preset mac-release-arm64
macOS x64: cmake --preset mac-release-x64 && cmake --build --preset mac-release-x64
Windows: cmake --preset win-release && cmake --build --preset win-release --config Release
Run: ./build/mac-release-arm64/pcap_manipulation_example <pcap> <json> [outdir] (mac) or .\build\win-release\Release\pcap_manipulation_example.exe <pcap> <json> [outdir] (win).
Optional packages: cmake --build --preset mac-release-arm64-package or cmake --build --preset win-release-package --config Release (requires Npcap runtime installed on Windows).
Files touched:

CMakeLists.txt
vcpkg.json
CMakePresets.json
tools/setup_vcpkg.sh
tools/setup_vcpkg.ps1
docu_after_git_pull.md
