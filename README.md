# Soapy SDR module for SDRPlay

This is a fork of [pothosware/SoapySDRPlay3](https://github.com/pothosware/SoapySDRPlay3) with additional features, bug fixes, and stability improvements.

## Documentation

* https://github.com/TobiasWooldridge/SoapySDRPlay3/wiki

## Dependencies

* SDRplay API v3.15+ - download from https://www.sdrplay.com/downloads
* SoapySDR - https://github.com/pothosware/SoapySDR

## Differences from Upstream

This fork includes several enhancements over the upstream pothosware/SoapySDRPlay3 repository. We aim to contribute improvements back upstream where appropriate.

### Multi-Device Support (Experimental)

The SDRplay API has a fundamental limitation: only one process can use the API at a time. This fork includes an experimental subprocess proxy architecture that works around this limitation:

```bash
# Enable multi-device mode
export SOAPY_SDRPLAY_MULTIDEV=1
```

When enabled, each device runs in a separate worker subprocess with:
- IPC command/status messaging via Unix pipes
- Lock-free shared memory ring buffer for streaming data
- Cross-process mutex for API call serialization

This allows multiple SDRplay devices to be used simultaneously from a single application.

### Accurate Gain Tables

The upstream driver uses a linear approximation for LNA gain that can be significantly inaccurate. This fork includes complete per-device, per-frequency LNA gain reduction tables from the SDRplay documentation:

| Device | Upstream Max Gain | Actual Max Gain | Improvement |
|--------|-------------------|-----------------|-------------|
| RSP1A @ 100 MHz | ~27 dB | 62 dB | +35 dB accuracy |
| RSPdx @ 100 MHz | ~27 dB | 84 dB | +57 dB accuracy |

The `setGain()` API now correctly maps requested gain to optimal LNA/IF combinations, and `getGainRange()` returns frequency-dependent valid ranges.

### Sample Gap Detection

Streaming callbacks now track `firstSampleNum` to detect when samples are dropped, logging warnings when discontinuities occur. This helps diagnose streaming issues.

### Service Timeout Protection

All blocking SDRplay API calls are wrapped with timeout protection to prevent indefinite hangs when the service becomes unresponsive. Includes automatic service health tracking and recovery attempts.

### Additional Enhancements

* **Hardware support**: RSP1B, RSPdx-R2, HDR mode with bandwidth controls
* **Device discovery**: Serial/mode filters, claimed-device enumeration for multi-client SoapyRemote
* **RSPduo**: Mode-specific device selection (ST/DT/MA/MA8/SL pseudo-devices), tuner switching safeguards, master/slave coordination
* **Streaming stability**: Safer teardown, deadlock/race fixes, ring buffer hot-path optimizations
* **Antenna persistence**: Per-device antenna settings stored under config dir
* **Build options**: USB bulk mode, serial-in-log, release optimizations, uninstall target

## Testing

* Unit tests cover deterministic helpers, stream buffer defaults, and readStream behavior
* Enable with `-DENABLE_TESTS=ON` and run `ctest --test-dir build`
* Unit tests default to a mock SDRplay API layer (`-DUSE_MOCK_SDRPLAY_API=ON`), avoiding hardware/service requirements (headers still required)
* Use `SOAPY_SDRPLAY_CONFIG_DIR` to override the config directory for antenna persistence (useful for tests or portable installs)
* Hardware-in-the-loop dual-radio stress test:
  `tests/run_hil_dual_read.sh SERIAL_A SERIAL_B` (or set `SDRPLAY_SERIAL_A`/`SDRPLAY_SERIAL_B`)
* HIL build target: `-DENABLE_HIL_TESTS=ON` creates `build/sdrplay_hil_dual_read`

## SDRplay Service Recovery

The SDRplay API service (`sdrplay_apiService`) can become unresponsive, causing device enumeration and `Device::make()` to hang indefinitely.

### Symptoms

- `SoapySDRUtil --find` hangs or times out
- Applications hang when opening SDRplay devices
- Log messages: "SDRplay API lock timed out" or "Device::make() timed out"
- SDRplay devices not detected despite being physically connected

### Recovery Script (macOS)

A recovery script is provided in `scripts/fix-sdrplay-full.sh`:

```bash
# Run with sudo (or configure passwordless sudo for this script)
sudo scripts/fix-sdrplay-full.sh
```

The script performs:
1. Kills all SDRplay service instances
2. Power-cycles SDRplay USB ports (requires `uhubctl`)
3. Restarts the SDRplay API service
4. Verifies device enumeration works

**Note:** You may need to customize the USB hub/port numbers in the script for your setup.

### Manual Recovery

```bash
# macOS
killall -9 sdrplay_apiService
/Library/SDRplayAPI/3.15.1/bin/sdrplay_apiService &

# Linux (systemd)
sudo systemctl restart sdrplay

# Verify
SoapySDRUtil --find=sdrplay
```

### Configuring Passwordless Sudo (macOS)

To allow the fix script to run without password prompts:

```bash
echo 'YOUR_USER ALL=(ALL) NOPASSWD: /path/to/SoapySDRPlay3/scripts/fix-sdrplay-full.sh' | sudo tee /etc/sudoers.d/fix-sdrplay
```

## Troubleshooting

This section contains some useful information for troubleshhoting

##### Message: `[WARNING] Can't find label in args`

An error message like this one:
```
Probe device driver=sdrplay
[WARNING] Can't find label in args
Error probing device: Can't find label in args
```

could be due to the OS not being able to 'see' the RSP as a USB device.

You may want to check using the command `lsusb`:
```
lsusb -d 1df7:
```
The output should look similar to this:
```
Bus 002 Device 006: ID 1df7:3010
```
If the `lsusb` command above returns nothing, it means the OS is not able to see the RSP (which could be due to a moltitude of reasons, like problems with the OS, bad USB cable, bad hardware, etc).

Another way to verify that the OS is able to see the RSP device is by running the `dmesg` command
```
dmesg
```
and look for lines similar to these (the idVendor value should be 1df7):
```
[ 1368.128506] usb 2-2: new high-speed USB device number 6 using xhci_hcd
[ 1368.255007] usb 2-2: New USB device found, idVendor=1df7, idProduct=3010, bcdDevice= 2.00
[ 1368.255016] usb 2-2: New USB device strings: Mfr=0, Product=0, SerialNumber=0
```

If there's nothing like that, try first to disconnect the RSP and then connect it back; if that does not work, try rebooting the computer; if that does not work either, try the RSP on a different computer with a different USB cable.


## Licensing information

The MIT License (MIT)

Copyright (c) 2015 Charles J. Cliffe<br/>
Copyright (c) 2020 Franco Venturi - changes for SDRplay API version 3 and Dual Tuner for RSPduo


Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
