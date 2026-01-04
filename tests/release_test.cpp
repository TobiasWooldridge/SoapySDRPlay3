// Quick test to diagnose ReleaseDevice timeout issue
// Build: g++ -std=c++17 -o release_test release_test.cpp -lsdrplay_api
// Run: ./release_test

#include <sdrplay_api.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>

// Simple callback - just track if it's called
static volatile int callbackCount = 0;

void streamCallback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params,
                    unsigned int numSamples, unsigned int reset, void *cbContext)
{
    callbackCount++;
}

void eventCallback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner,
                   sdrplay_api_EventParamsT *params, void *cbContext)
{
    std::cout << "Event: " << eventId << std::endl;
}

int main(int argc, char **argv)
{
    sdrplay_api_ErrT err;
    float ver = 0.0f;
    sdrplay_api_DeviceT rspDevs[6];
    unsigned int nDevs = 0;

    std::cout << "=== SDRplay API Direct Test ===" << std::endl;

    // Open API
    std::cout << "1. Opening API..." << std::flush;
    auto t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_Open();
    auto t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "sdrplay_api_Open failed: " << sdrplay_api_GetErrorString(err) << std::endl;
        return 1;
    }

    // Get version
    sdrplay_api_ApiVersion(&ver);
    std::cout << "API version: " << ver << std::endl;

    // Lock API
    std::cout << "2. Locking API..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_LockDeviceApi();
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;

    // Get devices
    std::cout << "3. Getting devices..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_GetDevices(rspDevs, &nDevs, 6);
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "GetDevices failed: " << sdrplay_api_GetErrorString(err) << std::endl;
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        return 1;
    }
    std::cout << "Found " << nDevs << " devices" << std::endl;

    if (nDevs == 0) {
        std::cerr << "No devices found!" << std::endl;
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        return 1;
    }

    // Select first device
    sdrplay_api_DeviceT *device = &rspDevs[0];
    std::cout << "4. Selecting device " << device->SerNo << "..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_SelectDevice(device);
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "SelectDevice failed: " << sdrplay_api_GetErrorString(err) << std::endl;
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        return 1;
    }

    // Unlock API
    std::cout << "5. Unlocking API..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    sdrplay_api_UnlockDeviceApi();
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;

    // Get device params
    sdrplay_api_DeviceParamsT *deviceParams;
    std::cout << "6. Getting device params..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_GetDeviceParams(device->dev, &deviceParams);
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "GetDeviceParams failed: " << sdrplay_api_GetErrorString(err) << std::endl;
        sdrplay_api_ReleaseDevice(device);
        sdrplay_api_Close();
        return 1;
    }

    // Set sample rate
    deviceParams->devParams->fsFreq.fsHz = 2000000.0;
    deviceParams->rxChannelA->tunerParams.rfFreq.rfHz = 100000000.0;
    deviceParams->rxChannelA->tunerParams.bwType = sdrplay_api_BW_0_300;
    deviceParams->rxChannelA->tunerParams.ifType = sdrplay_api_IF_Zero;

    // Init streaming
    sdrplay_api_CallbackFnsT cbFns;
    cbFns.StreamACbFn = streamCallback;
    cbFns.StreamBCbFn = streamCallback;
    cbFns.EventCbFn = eventCallback;

    std::cout << "7. Init streaming..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_Init(device->dev, &cbFns, nullptr);
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "Init failed: " << sdrplay_api_GetErrorString(err) << std::endl;
        sdrplay_api_ReleaseDevice(device);
        sdrplay_api_Close();
        return 1;
    }

    // Stream for a bit
    std::cout << "8. Streaming for 2 seconds..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << callbackCount << " callbacks)" << std::endl;

    // Uninit
    std::cout << "9. Uninit streaming..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_Uninit(device->dev);
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "Uninit failed: " << sdrplay_api_GetErrorString(err) << std::endl;
    }

    // Wait a moment for callbacks to fully stop
    std::cout << "10. Waiting 500ms for callbacks to stop..." << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << " done" << std::endl;

    // Release device
    std::cout << "11. Releasing device..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_ReleaseDevice(device);
    t1 = std::chrono::steady_clock::now();
    auto releaseTime = std::chrono::duration<double>(t1-t0).count();
    std::cout << " done (" << releaseTime << "s)" << std::endl;
    if (err != sdrplay_api_Success) {
        std::cerr << "ReleaseDevice failed: " << sdrplay_api_GetErrorString(err) << std::endl;
    }

    // Close API
    std::cout << "12. Closing API..." << std::flush;
    t0 = std::chrono::steady_clock::now();
    err = sdrplay_api_Close();
    t1 = std::chrono::steady_clock::now();
    std::cout << " done (" << std::chrono::duration<double>(t1-t0).count() << "s)" << std::endl;

    std::cout << "\n=== RESULT ===" << std::endl;
    if (releaseTime > 5.0) {
        std::cout << "FAIL: ReleaseDevice took " << releaseTime << "s (>5s)" << std::endl;
        return 1;
    } else {
        std::cout << "PASS: All operations completed in reasonable time" << std::endl;
        return 0;
    }
}
