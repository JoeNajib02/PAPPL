/**
 * Shared helper functions for repeatability filters.
 */
#pragma once

namespace ouster {
namespace sensor_utils {

/**
 * @brief Clamp a numeric value to be non-negative.
 * @tparam T Numeric type.
 * @param value Value to clamp.
 * @return 0 when value is negative, otherwise the original value.
 *
 * Used by filters to enforce non-negative ranges after smoothing operations.
 * Keeps the type intact (no casting to double/float) to minimize conversions.
 */
template <typename T>
inline T clamp_non_negative(T value) {
    return value < static_cast<T>(0) ? static_cast<T>(0) : value;
}

}  // namespace sensor_utils
}  // namespace ouster
