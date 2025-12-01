# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SoapySDRPlay3 is a Soapy SDR module that bridges SDRPlay radio devices (RSP series) with the Soapy SDR framework using the SDRplay API v3.15+. This enables SDRPlay devices to work with any application supporting the Soapy SDR API.

## Build Commands

```bash
# Standard build
mkdir build && cd build
cmake ..
make

# Install/uninstall (requires sudo)
sudo make install
sudo make uninstall
```

**CMake Configuration Options:**
- `-DRF_GAIN_IN_MENU=ON` - Add RF gain as a setting alongside IFGR gain
- `-DSTREAMING_USB_MODE_BULK=ON` - Use USB bulk mode instead of isochronous
- `-DSHOW_SERIAL_NUMBER_IN_MESSAGES=ON` - Show serial numbers in log messages

## Dependencies

- **SoapySDR v0.4.0+** - SDR framework
- **SDRplay API v3.15+** - Native driver library from sdrplay.com
- **C++11 compiler**

## Architecture

The codebase follows a modular structure with four implementation files:

### Core Components

1. **SoapySDRPlay.hpp** - Main class definition inheriting from `SoapySDR::Device`
   - Contains `SoapySDRPlay` device class
   - Nested `SoapySDRPlayStream` class for stream buffer management

2. **Registration.cpp** - Device enumeration and factory
   - `findSDRPlay()` discovers available devices
   - `makeSDRPlay()` factory function for instantiation
   - Special handling for RSPduo multi-mode devices (Single/Dual Tuner, Master/Slave)
   - Uses `selectedRSPDevices` static map to track claimed devices

3. **sdrplay_api.cpp** - Singleton API wrapper
   - Single instance per process (SDRplay API constraint)
   - Manages `sdrplay_api_Open()` and `sdrplay_api_Close()` lifecycle

4. **Settings.cpp** - Device configuration (largest file)
   - Frequency, gain, bandwidth, sample rate control
   - AGC configuration (default set point: -30dBfs)
   - Hardware version identification and RSPduo mode switching

5. **Streaming.cpp** - Real-time data streaming
   - Ring buffer implementation (8 buffers × 65536 elements default)
   - RX callback handlers from SDRplay API
   - Direct buffer access API support
   - Thread synchronization with `_general_state_mutex`

### Key Design Patterns

- **Singleton**: SDRplay API initialization (`sdrplay_api` class)
- **Ring Buffer**: Stream data buffering in `SoapySDRPlayStream`
- **Factory**: Device creation via SoapySDR registration macros
- **Callback**: Hardware event handling from SDRplay API

### Thread Safety

- Per-stream mutexes protect buffer operations
- Global `_general_state_mutex` protects device parameters
- Condition variables synchronize producer-consumer streaming

## Supported Hardware

RSP1, RSP1A, RSP1B, RSP2, RSPduo, RSPdx, RSPdx-R2

## Stream Formats

- CS16 (Complex Int16) - Native format
- CF32 (Complex Float32) - Converted
