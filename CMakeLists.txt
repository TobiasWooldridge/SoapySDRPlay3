########################################################################
# Build Soapy SDR support module for SDRplay (API version 3)
########################################################################
cmake_minimum_required(VERSION 3.10)
project(SoapySDRPlay CXX)

find_package(SoapySDR "0.4.0" NO_MODULE REQUIRED)
if (NOT SoapySDR_FOUND)
    message(FATAL_ERROR "Soapy SDR development files not found...")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LibSDRplay)

if (NOT LIBSDRPLAY_FOUND)
    message(FATAL_ERROR "SDRplay development files not found...")
endif ()
message(STATUS "LIBSDRPLAY_INCLUDE_DIRS - ${LIBSDRPLAY_INCLUDE_DIRS}")
message(STATUS "LIBSDRPLAY_LIBRARIES - ${LIBSDRPLAY_LIBRARIES}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${LIBSDRPLAY_INCLUDE_DIRS})

# As SoapySDR requires this, we can safely
# do the same
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler-specific settings
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Disable warnings for unused parameters
    add_compile_options(-Wno-unused-parameter)

    # Optimization flags for Release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

    # Enable fast floating-point optimizations (safe for SDR signal processing)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math")

    # Enable function sections for better dead code elimination
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
endif()

# Optional: Enable Link-Time Optimization for Release builds
option(ENABLE_LTO "Enable Link-Time Optimization (LTO)" OFF)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        message(STATUS "LTO enabled for Release builds")
    else()
        message(WARNING "LTO not supported: ${lto_error}")
    endif()
endif()

# Optional: Build for native CPU architecture (non-portable but faster)
option(ENABLE_NATIVE_ARCH "Build for native CPU architecture (not portable)" OFF)
if(ENABLE_NATIVE_ARCH)
    if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-march=native)
        message(STATUS "Building for native CPU architecture")
    endif()
endif()

# Configurable feature set
SET (RF_GAIN_IN_MENU ON CACHE BOOL "Add Rf gain as a setting, additionally to the IFGR gain control")

IF(RF_GAIN_IN_MENU)
    ADD_DEFINITIONS( -DRF_GAIN_IN_MENU=1 )
ENDIF()

SET (STREAMING_USB_MODE_BULK OFF CACHE BOOL "Use USB bulk mode instead of isochronous")

IF(STREAMING_USB_MODE_BULK)
    ADD_DEFINITIONS( -DSTREAMING_USB_MODE_BULK=1 )
ENDIF()

# Show serial number in messages
SET (SHOW_SERIAL_NUMBER_IN_MESSAGES "False" CACHE BOOL "Show serial number in log messages")
IF(SHOW_SERIAL_NUMBER_IN_MESSAGES)
    ADD_DEFINITIONS( -DSHOW_SERIAL_NUMBER_IN_MESSAGES )
ENDIF()

SOAPY_SDR_MODULE_UTIL(
    TARGET sdrPlaySupport
    SOURCES
        SoapySDRPlay.hpp
        Registration.cpp
        sdrplay_api.cpp
        DeviceControl.cpp
        Identification.cpp
        Antenna.cpp
        Gain.cpp
        Frequency.cpp
        SampleRate.cpp
        SettingsAPI.cpp
        Streaming.cpp
    LIBRARIES
        ${LIBSDRPLAY_LIBRARIES}
)

# gcc 10+ on Linux needs -pthread for pthread_cond_clockwait()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 10)
    target_link_options(sdrPlaySupport PRIVATE -pthread)
endif ()

########################################################################
# uninstall target
########################################################################
add_custom_target(uninstall
    "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

