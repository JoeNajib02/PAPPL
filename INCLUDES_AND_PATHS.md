# Include Path Notes for Standalone Project

## Problem in Official Repo

The header file `ouster_client/include/ouster/types.h` uses:
```cpp
#include "nonstd/optional.hpp"
```

This path is correct **within the official SDK tree**, but when using the SDK as an external dependency, the path may not resolve correctly depending on how it's installed.

## Solution in Standalone Project

The **standalone project's `CMakeLists.txt`** handles this automatically by:

1. Using `find_package(OusterSDK)` which sets up all necessary include paths
2. The SDK's already-installed headers have the correct paths
3. Your code includes only public Ouster SDK headers, so paths resolve correctly

**You don't need to do anything special!**

### How the Include Works

Your code:
```cpp
#include "ouster-pcap-manipulator/pcap_data_manipulator.h"
```

Which then includes:
```cpp
#include "ouster/types.h"       // From OusterSDK
#include "ouster/lidar_scan.h"  // From OusterSDK
```

CMake ensures all Ouster SDK paths are available via `find_package(OusterSDK)`.

---

## If You Get Include Errors

### Error: "optional.hpp: No such file"

**Cause**: SDK's optional-lite not in path

**Fix**: Make sure SDK is properly installed:
```bash
cmake --install . --prefix /path/to/install
export CMAKE_PREFIX_PATH=/path/to/install
cd /ouster-pcap-manipulator/build
cmake ..
```

### Error: "types.h: No such file"

**Cause**: OusterSDK not found

**Fix**:
```bash
# If SDK was installed by Homebrew:
cmake ..  # Should work automatically

# If SDK was built from source:
cmake .. -DCMAKE_PREFIX_PATH=/path/to/sdk/build/install

# Verify SDK is findable:
cmake .. -DCMAKE_PREFIX_PATH=/path/to/sdk/build/install -DCMAKE_FIND_DEBUG_MODE=ON
```

---

## Modifications to Official SDK (Reference)

If you ever need to reference what was changed in the official repo:

### 1. types.h (cosmetic, not needed for standalone)
```cpp
// OLD (still works in official repo):
#include "nonstd/optional.hpp"

// APPLIED (during dev, not needed for standalone):
#include "optional-lite/nonstd/optional.hpp"
```

This was a path fix for a specific build configuration. **Not needed when using the SDK as an external package.**

### 2. image_processing.cpp line 133 (potential bug fix)
```cpp
// OLD (may fail with Eigen mixed types):
key_eigen.max(0.0).min(1.0)

// APPLIED (fix):
key_eigen.max(static_cast<T>(0)).min(static_cast<T>(1))
```

This is a legitimate template fix. Consider if needed when upgrading SDK.

---

## Best Practice

For the **standalone project**, you don't need to worry about these. The official SDK installation handles all paths correctly.

Just use:
```cpp
#include "ouster/types.h"
#include "ouster/lidar_scan.h"
#include "ouster/pcap_packet_source.h"
// etc.
```

And CMake ensures the paths are resolved. ✅

