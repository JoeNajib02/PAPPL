cmake_minimum_required(VERSION 3.14)
project(ouster-pcap-manipulator VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Find Qt5/Qt6 (desktop widgets)
# ============================================================================
find_package(Qt5 COMPONENTS Core Gui Widgets QUIET)
if(NOT Qt5_FOUND)
    find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
    set(QT_PACKAGE_PREFIX Qt6)
    message(STATUS "[PCAP Manipulator] Found Qt6")
else()
    set(QT_PACKAGE_PREFIX Qt5)
    message(STATUS "[PCAP Manipulator] Found Qt5")
endif()

# ============================================================================
# Find libpcap and libtins
# ============================================================================
# Reset cached PCAP_LIBRARIES on Windows to allow refreshed detection
if(WIN32)
    unset(PCAP_LIBRARIES CACHE)
    unset(PCAP_LIBRARY CACHE)
    unset(PCAP_LIBRARY_RELEASE CACHE)
    unset(PCAP_LIBRARY_DEBUG CACHE)
endif()

# Try pkg-config first, fall back to direct library search
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PCAP libpcap)
endif()

if(PCAP_FOUND)
    if(PCAP_LIBRARIES)
        set(PCAP_LIBRARIES ${PCAP_LIBRARIES} ${PCAP_LDFLAGS})
    endif()
    if(PCAP_INCLUDE_DIRS OR PCAP_INCLUDE_DIR)
        set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIRS} ${PCAP_INCLUDE_DIR})
    endif()
endif()

if(NOT PCAP_FOUND)
    # Manual search for libpcap
    if(WIN32)
        find_library(PCAP_LIBRARY NAMES wpcap Packet)
        find_path(PCAP_INCLUDE_DIR NAMES pcap.h PATHS $ENV{NPFSDK_ROOT}/Include $ENV{ProgramW6432}/Npcap/Include)
    elseif(APPLE)
        find_library(PCAP_LIBRARY NAMES pcap libpcap HINTS /opt/homebrew/lib)
        find_path(PCAP_INCLUDE_DIR NAMES pcap.h HINTS /opt/homebrew/include)
    else()
        find_library(PCAP_LIBRARY NAMES pcap libpcap)
        find_path(PCAP_INCLUDE_DIR NAMES pcap/pcap.h pcap.h)
    endif()
    if(PCAP_LIBRARY AND PCAP_INCLUDE_DIR)
        set(PCAP_LIBRARIES ${PCAP_LIBRARY})
        set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})
    endif()
endif()

# Windows: vcpkg path hints if still unresolved
if(WIN32 AND CMAKE_TOOLCHAIN_FILE AND (NOT PCAP_LIBRARIES OR PCAP_LIBRARIES STREQUAL "pcap"))
    if(NOT VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    endif()
    get_filename_component(_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_vcpkg_root "${_vcpkg_root}/../.." ABSOLUTE)
    set(_pcap_release "${_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/lib/pcap.lib")
    set(_pcap_debug "${_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/debug/lib/pcap.lib")
    if(EXISTS "${_pcap_release}")
        set(PCAP_LIBRARY_RELEASE "${_pcap_release}" CACHE FILEPATH "Release libpcap" FORCE)
    endif()
    if(EXISTS "${_pcap_debug}")
        set(PCAP_LIBRARY_DEBUG "${_pcap_debug}" CACHE FILEPATH "Debug libpcap" FORCE)
    endif()
    if(PCAP_LIBRARY_RELEASE AND PCAP_LIBRARY_DEBUG)
        set(PCAP_LIBRARIES optimized ${PCAP_LIBRARY_RELEASE} debug ${PCAP_LIBRARY_DEBUG})
    elseif(PCAP_LIBRARY_RELEASE)
        set(PCAP_LIBRARIES ${PCAP_LIBRARY_RELEASE})
    elseif(PCAP_LIBRARY_DEBUG)
        set(PCAP_LIBRARIES ${PCAP_LIBRARY_DEBUG})
    endif()
    if(NOT PCAP_INCLUDE_DIRS)
        set(_pcap_inc "${_vcpkg_root}/installed/${VCPKG_TARGET_TRIPLET}/include")
        if(EXISTS "${_pcap_inc}")
            set(PCAP_INCLUDE_DIRS "${_pcap_inc}")
        endif()
    endif()
    set(PCAP_FOUND TRUE)
endif()

# Additional Windows fallback if still unresolved
if(WIN32 AND (NOT PCAP_LIBRARIES OR PCAP_LIBRARIES STREQUAL "pcap"))
    if(NOT VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    endif()
    if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    endif()
    if(VCPKG_ROOT)
        find_library(PCAP_LIBRARY_RELEASE NAMES pcap
            PATHS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib")
        find_library(PCAP_LIBRARY_DEBUG NAMES pcap
            PATHS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/lib")
        if(PCAP_LIBRARY_RELEASE AND PCAP_LIBRARY_DEBUG)
            set(PCAP_LIBRARIES optimized ${PCAP_LIBRARY_RELEASE} debug ${PCAP_LIBRARY_DEBUG})
        elseif(PCAP_LIBRARY_RELEASE)
            set(PCAP_LIBRARIES ${PCAP_LIBRARY_RELEASE})
        elseif(PCAP_LIBRARY_DEBUG)
            set(PCAP_LIBRARIES ${PCAP_LIBRARY_DEBUG})
        endif()
        if(NOT PCAP_INCLUDE_DIRS)
            find_path(PCAP_INCLUDE_DIR NAMES pcap.h
                PATHS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include")
            if(PCAP_INCLUDE_DIR)
                set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})
            endif()
        endif()
    endif()
endif()

# Final Windows resolution pass to get absolute pcap.lib
if(WIN32 AND (NOT PCAP_LIBRARIES OR PCAP_LIBRARIES STREQUAL "pcap"))
    if(NOT VCPKG_TARGET_TRIPLET)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    endif()
    set(_pcap_hint_roots "")
    if(DEFINED VCPKG_ROOT)
        list(APPEND _pcap_hint_roots "${VCPKG_ROOT}")
    endif()
    if(NOT _pcap_hint_roots AND DEFINED ENV{VCPKG_ROOT})
        list(APPEND _pcap_hint_roots "$ENV{VCPKG_ROOT}")
    endif()
    if(DEFINED _vcpkg_root)
        list(APPEND _pcap_hint_roots "${_vcpkg_root}")
    endif()
    list(APPEND _pcap_hint_roots "C:/vcpkg" "D:/classic-vcpkg")

    foreach(_root IN LISTS _pcap_hint_roots)
        set(_rel_path "${_root}/installed/${VCPKG_TARGET_TRIPLET}/lib/pcap.lib")
        set(_dbg_path "${_root}/installed/${VCPKG_TARGET_TRIPLET}/debug/lib/pcap.lib")
        if(EXISTS "${_rel_path}" OR EXISTS "${_dbg_path}")
            if(EXISTS "${_rel_path}" AND EXISTS "${_dbg_path}")
                set(PCAP_LIBRARIES optimized "${_rel_path}" debug "${_dbg_path}")
            elseif(EXISTS "${_rel_path}")
                set(PCAP_LIBRARIES "${_rel_path}")
            elseif(EXISTS "${_dbg_path}")
                set(PCAP_LIBRARIES "${_dbg_path}")
            endif()
            if(NOT PCAP_INCLUDE_DIRS)
                set(_pcap_inc "${_root}/installed/${VCPKG_TARGET_TRIPLET}/include")
                if(EXISTS "${_pcap_inc}")
                    set(PCAP_INCLUDE_DIRS "${_pcap_inc}")
                endif()
            endif()
            break()
        endif()
    endforeach()
endif()

# Derive lib path from include dir if still unresolved on Windows
if(WIN32 AND (NOT PCAP_LIBRARIES OR PCAP_LIBRARIES STREQUAL "pcap") AND PCAP_INCLUDE_DIRS)
    unset(PCAP_LIBRARY_RELEASE CACHE)
    unset(PCAP_LIBRARY_DEBUG CACHE)
    foreach(_inc_dir IN LISTS PCAP_INCLUDE_DIRS)
        file(TO_CMAKE_PATH "${_inc_dir}" _inc_norm)
        if(_inc_norm MATCHES "(.*/installed/[^/]+)/(include)")
            set(_root "${CMAKE_MATCH_1}")
            find_library(PCAP_LIBRARY_RELEASE NAMES pcap PATHS "${_root}/lib")
            find_library(PCAP_LIBRARY_DEBUG NAMES pcap PATHS "${_root}/debug/lib")
            if(PCAP_LIBRARY_RELEASE AND PCAP_LIBRARY_DEBUG)
                set(PCAP_LIBRARIES optimized ${PCAP_LIBRARY_RELEASE} debug ${PCAP_LIBRARY_DEBUG})
                break()
            elseif(PCAP_LIBRARY_RELEASE)
                set(PCAP_LIBRARIES ${PCAP_LIBRARY_RELEASE})
                break()
            elseif(PCAP_LIBRARY_DEBUG)
                set(PCAP_LIBRARIES ${PCAP_LIBRARY_DEBUG})
                break()
            endif()
        endif()
    endforeach()
endif()

# On Windows, prefer resolved absolute libs and strip pkg-config style flags.
if(WIN32 AND PCAP_LIBRARIES)
    set(_pcap_clean "")
    foreach(_lib IN LISTS PCAP_LIBRARIES)
        if(_lib MATCHES "^-l")
            continue()
        elseif(_lib MATCHES "^-L")
            continue()
        else()
            list(APPEND _pcap_clean "${_lib}")
        endif()
    endforeach()
    if(_pcap_clean)
        set(PCAP_LIBRARIES ${_pcap_clean})
    endif()
endif()

find_package(libtins REQUIRED)

# ============================================================================
# Find Eigen3
# ============================================================================
find_package(Eigen3 REQUIRED)
message(STATUS "[PCAP Manipulator] Found Eigen3")

# ============================================================================
# Configuration: Build ouster-sdk from raw source or installed package
# ============================================================================
option(OUSTER_USE_INSTALLED_SDK "Use installed OusterSDK via find_package" OFF)
option(OUSTER_SDK_FETCH "Automatically fetch OusterSDK into the tree when not found" ON)
set(OUSTER_SDK_GIT_TAG "64a6cbe6dff1eea0cb5a79d63b79acf11b68b16d" CACHE STRING "Git tag or commit for OusterSDK when auto-fetching")
set(OUSTER_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/external/ouster-sdk" CACHE PATH "Path to ouster-sdk source")
# Keep ouster-sdk lean for our use case: disable viz/tests/examples to avoid extra deps (GLEW, Qt extras)
set(BUILD_VIZ OFF CACHE BOOL "Disable Ouster SDK viz" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable Ouster SDK tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable Ouster SDK examples" FORCE)

if(OUSTER_USE_INSTALLED_SDK)
    find_package(OusterSDK REQUIRED)
    message(STATUS "[PCAP Manipulator] Using installed OusterSDK package")
else()
    if(EXISTS "${OUSTER_SDK_SOURCE}/CMakeLists.txt")
        add_subdirectory("${OUSTER_SDK_SOURCE}" "${CMAKE_BINARY_DIR}/ouster_sdk_build")
        set(OUSTER_SDK_DIR "${CMAKE_BINARY_DIR}/ouster_sdk_build")
        message(STATUS "[PCAP Manipulator] Using local OusterSDK source: ${OUSTER_SDK_SOURCE}")
    elseif(OUSTER_SDK_FETCH)
        include(FetchContent)
        set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
        FetchContent_Declare(
            ouster_sdk
            GIT_REPOSITORY https://github.com/ouster-lidar/ouster-sdk.git
            GIT_TAG        ${OUSTER_SDK_GIT_TAG}
            SOURCE_DIR     "${OUSTER_SDK_SOURCE}"
        )
        FetchContent_MakeAvailable(ouster_sdk)
        set(OUSTER_SDK_DIR "${ouster_sdk_BINARY_DIR}")
        message(STATUS "[PCAP Manipulator] Fetched OusterSDK: ${ouster_sdk_SOURCE_DIR} @ ${OUSTER_SDK_GIT_TAG}")
    endif()
    if(NOT TARGET ouster_client)
        message(FATAL_ERROR "[PCAP Manipulator] OusterSDK source not available. Set OUSTER_SDK_SOURCE or enable OUSTER_SDK_FETCH.")
    endif()
endif()

# Report libs after all resolution
message(STATUS "[PCAP Manipulator] Using libpcap libs: ${PCAP_LIBRARIES}")
message(STATUS "[PCAP Manipulator] Using libpcap includes: ${PCAP_INCLUDE_DIRS}")
message(STATUS "[PCAP Manipulator] Found libtins")

# ============================================================================
# PCAP Manipulator Library Target
# ============================================================================

if(WIN32)
    set(REPEATABILITY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/repeatability_filters.cpp)
    set(REPEATABILITY_HDR ${CMAKE_CURRENT_SOURCE_DIR}/src/repeatability_filters.h)
else()
    set(REPEATABILITY_SRC src/repeatability_filters.cpp)
    set(REPEATABILITY_HDR src/repeatability_filters.h)
endif()

add_library(pcap_data_manipulator STATIC
    src/pcap_data_manipulator.cpp
    ${REPEATABILITY_SRC}
    src/scan_statistics.cpp
    src/scan_filters.cpp
    src/rail_profile_processor.cpp
)

# Include directories for library
target_include_directories(pcap_data_manipulator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include/ouster-pcap-manipulator>
        $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/examples>
        ${OUSTER_SDK_SOURCE}/ouster_client/include
        ${CMAKE_BINARY_DIR}/ouster_sdk_build/generated
        ${OUSTER_SDK_SOURCE}/ouster_pcap/include
)

# Link libraries for manipulator - use ouster targets from source build
target_link_libraries(pcap_data_manipulator
    PUBLIC
        Eigen3::Eigen
        ouster_client
        ouster_pcap
        ouster_sensor
        ${PCAP_LIBRARIES}
        tins
)

message(STATUS "[PCAP Manipulator] Library target configured")

# ============================================================================
# Example Executable
# ============================================================================

add_executable(pcap_manipulation_example
    examples/pcap_manipulation_example.cpp
    examples/demo_runner.cpp
)

# Link to the manipulator library and system dependencies
target_link_libraries(pcap_manipulation_example
    PRIVATE
        pcap_data_manipulator
        Eigen3::Eigen
        ouster_client
        ouster_pcap
        ouster_sensor
        ${PCAP_LIBRARIES}
        tins
)

add_executable(test_all_classes
    examples/test_all_classes.cpp
)

target_link_libraries(test_all_classes
    PRIVATE
        pcap_data_manipulator
        Eigen3::Eigen
        ouster_client
        ouster_pcap
        ouster_sensor
        ${PCAP_LIBRARIES}
        tins
)

add_executable(compare_point_repeatability
    examples/compare_point_repeatability.cpp
)

target_link_libraries(compare_point_repeatability
    PRIVATE
        pcap_data_manipulator
        Eigen3::Eigen
        ouster_client
        ouster_pcap
        ouster_sensor
        ${PCAP_LIBRARIES}
        tins
)

message(STATUS "[PCAP Manipulator] Library target configured")

# ============================================================================
# GUI Executable
# ============================================================================

add_executable(pcap_gui
    gui/main.cpp
    gui/pcap_gui.cpp
    gui/pcap_gui.h
)

# Link to the manipulator library and Qt
target_link_libraries(pcap_gui
    PRIVATE
        pcap_data_manipulator
        Eigen3::Eigen
        ouster_client
        ouster_pcap
        ouster_sensor
        ${PCAP_LIBRARIES}
        tins
        ${QT_PACKAGE_PREFIX}::Core
        ${QT_PACKAGE_PREFIX}::Gui
        ${QT_PACKAGE_PREFIX}::Widgets
)

message(STATUS "[PCAP Manipulator] GUI target configured")

# ============================================================================
# Rail profile example (CLI)
# ============================================================================

add_executable(rail_profile_example
    examples/rail_profile_example.cpp
)

target_link_libraries(rail_profile_example
    PRIVATE
        pcap_data_manipulator
        Eigen3::Eigen
)

# ============================================================================
# Installation Configuration
# ============================================================================

install(TARGETS pcap_data_manipulator
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

install(FILES src/pcap_data_manipulator.h
              ${REPEATABILITY_HDR}
              src/scan_statistics.h
              src/scan_filters.h
              src/rail_profile_processor.h
        DESTINATION include/ouster-pcap-manipulator
)

install(DIRECTORY examples/
        DESTINATION examples
        FILES_MATCHING PATTERN "*.cpp"
)

install(FILES
        README.md
        LICENSE
        DESTINATION .
)

# ============================================================================
# Build Information
# ============================================================================

message(STATUS "[PCAP Manipulator] Build Configuration Summary:")
message(STATUS "  Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  OusterSDK Dir: ${OUSTER_SDK_DIR}")
message(STATUS "  Targets: pcap_manipulation_example, pcap_gui, rail_profile_example")
message(STATUS "")

# ============================================================================
# Packaging
# ============================================================================
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
include(CPack)
