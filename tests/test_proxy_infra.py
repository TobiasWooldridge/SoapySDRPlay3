#!/usr/bin/env python3
"""Test the subprocess multi-device infrastructure without hardware."""

import sys
import os
import time

# Test 1: Verify module loads with proxy support
print("Test 1: Loading SoapySDR module...")
try:
    import SoapySDR
    print("  SoapySDR loaded successfully")
except ImportError as e:
    print(f"  FAIL: Could not load SoapySDR: {e}")
    sys.exit(1)

# Test 2: Check if sdrplay driver is available
print("\nTest 2: Checking sdrplay driver availability...")
drivers = SoapySDR.listModules()
print(f"  Loaded modules: {len(drivers)}")
for d in drivers:
    print(f"    - {d}")

# Test 3: Try to find devices (may fail if service is down, that's OK)
print("\nTest 3: Enumerating SDRplay devices...")
try:
    results = SoapySDR.Device.enumerate({"driver": "sdrplay"})
    print(f"  Found {len(results)} SDRplay device(s)")
    for r in results:
        print(f"    - {r}")
except Exception as e:
    print(f"  Note: Enumeration failed (service may be down): {e}")

# Test 4: Try proxy mode with fake serial (should fail gracefully)
print("\nTest 4: Testing proxy mode device creation...")
os.environ["SOAPY_SDRPLAY_MULTIDEV"] = "1"
try:
    # This will fail but should show proxy mode is being used
    dev = SoapySDR.Device({"driver": "sdrplay", "serial": "TESTSERIAL"})
    print("  Device created (unexpected)")
    del dev
except Exception as e:
    error_msg = str(e)
    if "proxy" in error_msg.lower() or "worker" in error_msg.lower() or "subprocess" in error_msg.lower():
        print(f"  PASS: Proxy mode detected in error: {e}")
    else:
        print(f"  Note: Device creation failed: {e}")

print("\n=== Infrastructure Tests Complete ===")
