/**
 * Shared helper functions for repeatability filters.
 */
#pragma once

namespace ouster {
namespace sensor_utils {

template <typename T>
inline T clamp_non_negative(T value) {
    return value < static_cast<T>(0) ? static_cast<T>(0) : value;
}

}  // namespace sensor_utils
}  // namespace ouster

