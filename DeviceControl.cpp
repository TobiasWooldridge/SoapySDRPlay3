/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2020 Franco Venturi - changes for SDRplay API version 3
 *                                     and Dual Tuner for RSPduo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapySDRPlay.hpp"

#if defined(_M_X64) || defined(_M_IX86)
#define strcasecmp _stricmp
#elif defined (__GNUC__)
#include <strings.h>
#endif

std::unordered_map<std::string, sdrplay_api_DeviceT*> SoapySDRPlay::selectedRSPDevices;
static std::mutex selectedRSPDevices_mutex;

std::set<std::string> &SoapySDRPlay_getClaimedSerials(void)
{
   static std::set<std::string> serials;
   return serials;
}

/*******************************************************************
 * Constructor / Destructor
 ******************************************************************/

SoapySDRPlay::SoapySDRPlay(const SoapySDR::Kwargs &args)
{
    if (args.count("serial") == 0) throw std::runtime_error("no available RSP devices found");

    // Initialize atomics and stream pointers BEFORE selectDevice() which may trigger callbacks
    _streams[0] = nullptr;
    _streams[1] = nullptr;
    _streamsRefCount[0] = 0;
    _streamsRefCount[1] = 0;
    streamActive = false;
    device_unavailable = false;
    gr_changed = 0;
    rf_changed = 0;
    fs_changed = 0;
    useShort = true;
    shortsPerWord = 1;
    bufferLength = bufferElems * elementsPerSample * shortsPerWord;
    cachedBufferThreshold = bufferLength.load();  // Initially no decimation

    selectDevice(args.at("serial"),
                 args.count("mode") ? args.at("mode") : "",
                 args.count("antenna") ? args.at("antenna") : "");

    // keep all the default settings:
    // - rf: 200MHz
    // - fs: 2MHz
    // - decimation: off
    // - IF: 0kHz (zero IF)
    // - bw: 200kHz
    // - attenuation: 50dB
    // - LNA state: 0
    // - AGC: 50Hz
    // - DC correction: on
    // - IQ balance: on

    // change the default AGC set point to -30dBfs
    chParams->ctrlParams.agc.setPoint_dBfs = -30;

    // process additional device string arguments
    for (const auto &arg : args) {
        // ignore 'driver', 'label', 'mode', 'serial', and 'soapy'
        if (arg.first == "driver" || arg.first == "label" ||
            arg.first == "mode" || arg.first == "serial" ||
            arg.first == "soapy") {
            continue;
        }
        writeSetting(arg.first, arg.second);
    }

    cacheKey = serNo;
    if (hwVer == SDRPLAY_RSPduo_ID) cacheKey += "@" + args.at("mode");
    SoapySDRPlay_getClaimedSerials().insert(cacheKey);
}

SoapySDRPlay::~SoapySDRPlay(void)
{
    SoapySDRPlay_getClaimedSerials().erase(cacheKey);
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    releaseDevice();

    _streams[0] = nullptr;
    _streams[1] = nullptr;
    _streamsRefCount[0] = 0;
    _streamsRefCount[1] = 0;
}

/*******************************************************************
 * Device Selection / Release
 ******************************************************************/

void SoapySDRPlay::selectDevice(const std::string &serial,
                                const std::string &mode,
                                const std::string &antenna)
{
    serNo = serial;
    rspDeviceId = serial;
    if (mode == "SL") {
        rspDeviceId += "/S";
    }

    sdrplay_api_TunerSelectT tuner;
    sdrplay_api_RspDuoModeT rspDuoMode;
    double rspDuoSampleFreq = 0.0;
    if (mode.empty())
    {
        tuner = sdrplay_api_Tuner_Neither;
        rspDuoMode = sdrplay_api_RspDuoMode_Unknown;
        rspDuoSampleFreq = 0.0;
    }
    else if (mode == "ST")
    {
        tuner = sdrplay_api_Tuner_A;
        rspDuoMode = sdrplay_api_RspDuoMode_Single_Tuner;
        rspDuoSampleFreq = 0.0;
    }
    else if (mode == "DT")
    {
        tuner = sdrplay_api_Tuner_Both;
        rspDuoMode = sdrplay_api_RspDuoMode_Dual_Tuner;
        rspDuoSampleFreq = 6000000;
    }
    else if (mode == "MA")
    {
        tuner = sdrplay_api_Tuner_A;
        rspDuoMode = sdrplay_api_RspDuoMode_Master;
        rspDuoSampleFreq = 6000000;
    }
    else if (mode == "MA8")
    {
        tuner = sdrplay_api_Tuner_A;
        rspDuoMode = sdrplay_api_RspDuoMode_Master;
        rspDuoSampleFreq = 8000000;
    }
    else if (mode == "SL")
    {
        tuner = sdrplay_api_Tuner_Neither;
        rspDuoMode = sdrplay_api_RspDuoMode_Slave;
    }
    else
    {
        throw std::runtime_error("sdrplay RSPduo mode is invalid");
    }

    // if an antenna is specified, select the RSPduo tuner based on it
    if (!(rspDuoMode == sdrplay_api_RspDuoMode_Unknown ||
          rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner))
    {
        if (!antenna.empty())
        {
            if (antenna == "Tuner 1 50 ohm") {
                tuner = sdrplay_api_Tuner_A;
            } else if (antenna == "Tuner 1 Hi-Z") {
                tuner = sdrplay_api_Tuner_A;
            } else if (antenna == "Tuner 2 50 ohm") {
                tuner = sdrplay_api_Tuner_B;
            } else {
                throw std::runtime_error("invalid RSPduo antenna selected");
            }
        }
    }

    selectDevice(tuner, rspDuoMode, rspDuoSampleFreq, nullptr);

    return;
}

void SoapySDRPlay::selectDevice()
{
    // Prevent device re-selection while streaming is active to avoid
    // invalidating pointers used by callbacks
    if (streamActive) {
        return;
    }

    bool needsReselect = false;
    {
        std::lock_guard<std::mutex> lock(selectedRSPDevices_mutex);
        if (selectedRSPDevices.count(rspDeviceId) > 0 &&
            selectedRSPDevices.at(rspDeviceId) != &device) {
            needsReselect = true;
        }
    }
    if (needsReselect) {
        selectDevice(device.tuner, device.rspDuoMode, device.rspDuoSampleFreq,
                     deviceParams);
    }
    return;
}

void SoapySDRPlay::selectDevice(sdrplay_api_TunerSelectT tuner,
                                sdrplay_api_RspDuoModeT rspDuoMode,
                                double rspDuoSampleFreq,
                                sdrplay_api_DeviceParamsT *thisDeviceParams)
{
    sdrplay_api_ErrT err;
    {
        std::lock_guard<std::mutex> lock(selectedRSPDevices_mutex);
        if (selectedRSPDevices.count(rspDeviceId)) {
            sdrplay_api_DeviceT *currDevice = selectedRSPDevices.at(rspDeviceId);
            selectedRSPDevices.erase(rspDeviceId);
            err = sdrplay_api_ReleaseDevice(currDevice);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_ERROR, "ReleaseDevice Error: %s", sdrplay_api_GetErrorString(err));
                throw std::runtime_error("ReleaseDevice() failed");
            }
        }
    }

    // save all the device configuration so we can put it back later on
    bool hasDevParams = false;
    bool hasRxChannelA = false;
    bool hasRxChannelB = false;
    sdrplay_api_DevParamsT devParams;
    sdrplay_api_RxChannelParamsT rxChannelA;
    sdrplay_api_RxChannelParamsT rxChannelB;
    if (thisDeviceParams)
    {
        hasDevParams = thisDeviceParams->devParams;
        hasRxChannelA = thisDeviceParams->rxChannelA;
        hasRxChannelB = thisDeviceParams->rxChannelB;
        if (hasDevParams) devParams = *thisDeviceParams->devParams;
        if (hasRxChannelA) rxChannelA = *thisDeviceParams->rxChannelA;
        if (hasRxChannelB) rxChannelB = *thisDeviceParams->rxChannelB;
    }

    // retrieve hwVer and serNo by API
    unsigned int nDevs = 0;

    SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: requesting serial=%s, rspDeviceId=%s", serNo.c_str(), rspDeviceId.c_str());
    {
        std::lock_guard<std::mutex> lock(selectedRSPDevices_mutex);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: currently %zu devices in selectedRSPDevices map", selectedRSPDevices.size());
        for (const auto& kv : selectedRSPDevices) {
            SoapySDR_logf(SOAPY_SDR_INFO, "  - selected device: %s -> dev=%p", kv.first.c_str(), (void*)kv.second->dev);
        }
    }

    {
        SdrplayApiLockGuard apiLock;
        sdrplay_api_DeviceT rspDevs[SDRPLAY_MAX_DEVICES];
        sdrplay_api_GetDevices(&rspDevs[0], &nDevs, SDRPLAY_MAX_DEVICES);

        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: GetDevices returned %u devices", nDevs);

        unsigned devIdx = SDRPLAY_MAX_DEVICES;
        for (unsigned int i = 0; i < nDevs; i++)
        {
            SoapySDR_logf(SOAPY_SDR_INFO, "  [%u] SerNo=%s hwVer=%d valid=%d dev=%p",
                          i, rspDevs[i].SerNo, rspDevs[i].hwVer, rspDevs[i].valid, (void*)rspDevs[i].dev);
            if (!rspDevs[i].valid) continue;
            if (rspDevs[i].SerNo == serNo) devIdx = i;
        }
        if (devIdx == SDRPLAY_MAX_DEVICES) {
            SoapySDR_log(SOAPY_SDR_ERROR, "no sdrplay device matches");
            throw std::runtime_error("no sdrplay device matches");
        }

        device = rspDevs[devIdx];
        hwVer = device.hwVer;

        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: selected devIdx=%d", devIdx);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: SerNo=%s", device.SerNo);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: hwVer=%d", device.hwVer);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: dev handle=%p", (void*)device.dev);

        if (hwVer == SDRPLAY_RSPduo_ID && rspDuoMode != sdrplay_api_RspDuoMode_Slave)
        {
            if ((rspDuoMode & device.rspDuoMode) != rspDuoMode)
            {
                throw std::runtime_error("sdrplay RSPduo mode not available");
            }
            else
            {
                device.rspDuoMode = rspDuoMode;
            }
            if ((tuner & device.tuner) != tuner)
            {
                throw std::runtime_error("sdrplay RSPduo tuner not available");
            }
            else
            {
                device.tuner = tuner;
            }
            if (rspDuoSampleFreq != 0)
            {
                device.rspDuoSampleFreq = rspDuoSampleFreq;
            }
        }
        else if (hwVer == SDRPLAY_RSPduo_ID && rspDuoMode == sdrplay_api_RspDuoMode_Slave)
        {
            if (rspDuoMode != device.rspDuoMode)
            {
                throw std::runtime_error("sdrplay RSPduo slave mode not available");
            }
            if (tuner != sdrplay_api_Tuner_Neither && tuner != device.tuner)
            {
                throw std::runtime_error("sdrplay RSPduo tuner not available in slave mode");
            }
            if (rspDuoSampleFreq != 0 && rspDuoSampleFreq != device.rspDuoSampleFreq)
            {
                throw std::runtime_error("sdrplay RSPduo sample rate not available in slace mode");
            }
        }
        else
        {
            if (rspDuoMode != sdrplay_api_RspDuoMode_Unknown || tuner != sdrplay_api_Tuner_Neither)
            {
                throw std::runtime_error("sdrplay RSP does not support RSPduo mode or tuner");
            }
        }

        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: rspDuoMode=%d", device.rspDuoMode);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: tuner=%d", device.tuner);
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: rspDuoSampleFreq=%lf", device.rspDuoSampleFreq);

        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: calling sdrplay_api_SelectDevice for serial=%s dev=%p",
                      device.SerNo, (void*)device.dev);
        err = sdrplay_api_SelectDevice(&device);
        if (err != sdrplay_api_Success)
        {
            // Get extended error info
            sdrplay_api_ErrorInfoT *errInfo = sdrplay_api_GetLastError(nullptr);
            if (errInfo) {
                SoapySDR_logf(SOAPY_SDR_ERROR, "SelectDevice LastError: file=%s func=%s line=%d msg=%s",
                              errInfo->file, errInfo->function, errInfo->line, errInfo->message);
            }
            SoapySDR_logf(SOAPY_SDR_ERROR, "SelectDevice Error: %s (code=%d)", sdrplay_api_GetErrorString(err), err);
            throw std::runtime_error("SelectDevice() failed");
        }
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: SUCCESS! dev handle is now=%p", (void*)device.dev);
        {
            std::lock_guard<std::mutex> lock(selectedRSPDevices_mutex);
            selectedRSPDevices[rspDeviceId] = &device;
        }
        SoapySDR_logf(SOAPY_SDR_INFO, "selectDevice: stored in selectedRSPDevices[%s]", rspDeviceId.c_str());
    } // RAII guard releases API lock here

    // Enable (= sdrplay_api_DbgLvl_Verbose) API calls tracing,
    // but only for debug purposes due to its performance impact.
    sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Disable);
    //sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Verbose);

    err = sdrplay_api_GetDeviceParams(device.dev, &deviceParams);
    if (err != sdrplay_api_Success)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "GetDeviceParams Error: %s", sdrplay_api_GetErrorString(err));
        throw std::runtime_error("GetDeviceParams() failed");
    }

    if (thisDeviceParams)
    {
        if (hasDevParams) *deviceParams->devParams = devParams;
        if (hasRxChannelA) *deviceParams->rxChannelA = rxChannelA;
        if (hasRxChannelB) *deviceParams->rxChannelB = rxChannelB;
    }

    chParams = device.tuner == sdrplay_api_Tuner_B ? deviceParams->rxChannelB : deviceParams->rxChannelA;

    return;
}

void SoapySDRPlay::releaseDevice()
{
    sdrplay_api_ErrT err;
    sdrplay_api_DeviceT *currDevice = nullptr;
    {
        std::lock_guard<std::mutex> lock(selectedRSPDevices_mutex);
        if (selectedRSPDevices.count(rspDeviceId)) {
            currDevice = selectedRSPDevices.at(rspDeviceId);
            if (currDevice != &device) {
                // nothing to do - we are good
                return;
            }
            selectedRSPDevices.erase(rspDeviceId);
        }
    }
    if (currDevice) {
        err = sdrplay_api_ReleaseDevice(currDevice);
        if (err != sdrplay_api_Success)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "ReleaseDevice Error: %s", sdrplay_api_GetErrorString(err));
            throw std::runtime_error("ReleaseDevice() failed");
        }
    }

    return;
}

/*******************************************************************
 * Logging helpers (when SHOW_SERIAL_NUMBER_IN_MESSAGES is defined)
 ******************************************************************/

#ifdef SHOW_SERIAL_NUMBER_IN_MESSAGES
void SoapySDRPlay::SoapySDR_log(const SoapySDRLogLevel logLevel,
                                const char *message) const
{
    std::string message_with_info_string = "[S/N=" + serNo + "] - " + message;
    const char *message_with_info = message_with_info_string.c_str();
    ::SoapySDR_log(logLevel, message_with_info);
}

void SoapySDRPlay::SoapySDR_logf(const SoapySDRLogLevel logLevel,
                                const char *format, ...) const
{
    va_list argList;
    va_start(argList, format);
    std::string format_with_info_string = "[S/N=" + serNo + "] - " + format;
    const char *format_with_info = format_with_info_string.c_str();
    ::SoapySDR_vlogf(logLevel, format_with_info, argList);
    va_end(argList);
}
#endif
