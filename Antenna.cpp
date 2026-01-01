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

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static sdrplay_api_ErrT updateLocked(HANDLE dev,
                                     sdrplay_api_TunerSelectT tuner,
                                     sdrplay_api_ReasonForUpdateT reason,
                                     sdrplay_api_ReasonForUpdateExtension1T reasonExt)
{
    SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
    return sdrplay_api_Update(dev, tuner, reason, reasonExt);
}

namespace {
#ifdef _WIN32
const char kPathSep = '\\';
#else
const char kPathSep = '/';
#endif

std::string sanitizeKey(const std::string &key)
{
    std::string out;
    out.reserve(key.size());
    for (char ch : key)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')
        {
            out.push_back(ch);
        }
        else
        {
            out.push_back('_');
        }
    }
    return out;
}

std::string trimLine(std::string value)
{
    while (!value.empty())
    {
        char ch = value.back();
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t')
        {
            value.pop_back();
            continue;
        }
        break;
    }
    return value;
}

bool ensureDirExists(const std::string &path)
{
#ifdef _WIN32
    struct _stat st;
    if (_stat(path.c_str(), &st) == 0)
    {
        return (st.st_mode & _S_IFDIR) != 0;
    }
    if (_mkdir(path.c_str()) == 0)
    {
        return true;
    }
    return errno == EEXIST;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path.c_str(), 0755) == 0)
    {
        return true;
    }
    return errno == EEXIST;
#endif
}

std::string getConfigDir()
{
    const char *overridePath = std::getenv("SOAPY_SDRPLAY_CONFIG_DIR");
    if (overridePath && *overridePath)
    {
        return std::string(overridePath);
    }
#ifdef _WIN32
    const char *appdata = std::getenv("APPDATA");
    if (appdata && *appdata)
    {
        return std::string(appdata) + "\\SoapySDRPlay";
    }
    const char *userprofile = std::getenv("USERPROFILE");
    if (userprofile && *userprofile)
    {
        return std::string(userprofile) + "\\.config\\SoapySDRPlay";
    }
#else
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
    {
        return std::string(xdg) + "/SoapySDRPlay";
    }
    const char *home = std::getenv("HOME");
    if (home && *home)
    {
        return std::string(home) + "/.config/SoapySDRPlay";
    }
#endif
    return std::string();
}

std::string buildAntennaPath(const std::string &dir, const std::string &key, const size_t channel)
{
    std::ostringstream path;
    path << dir << kPathSep << "antenna_" << sanitizeKey(key) << "_ch" << channel << ".txt";
    return path.str();
}
} // namespace

std::string SoapySDRPlay::makeAntennaPersistKey(const std::string &serial, const std::string &mode)
{
    if (mode.empty())
    {
        return serial;
    }
    return serial + "@" + mode;
}

std::string SoapySDRPlay::loadPersistedAntenna(const std::string &key, const size_t channel) const
{
    if (key.empty())
    {
        return std::string();
    }
    const std::string dir = getConfigDir();
    if (dir.empty())
    {
        return std::string();
    }
    const std::string path = buildAntennaPath(dir, key, channel);
    std::ifstream input(path.c_str());
    if (!input.is_open())
    {
        return std::string();
    }
    std::string value;
    std::getline(input, value);
    return trimLine(value);
}

void SoapySDRPlay::savePersistedAntenna(const std::string &key, const size_t channel, const std::string &name) const
{
    if (key.empty() || name.empty())
    {
        return;
    }
    const std::string dir = getConfigDir();
    if (dir.empty())
    {
        return;
    }
    if (!ensureDirExists(dir))
    {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Antenna persistence disabled: cannot create config dir '%s'", dir.c_str());
        return;
    }
    const std::string path = buildAntennaPath(dir, key, channel);
    std::ofstream output(path.c_str(), std::ofstream::trunc);
    if (!output.is_open())
    {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Antenna persistence disabled: cannot write '%s'", path.c_str());
        return;
    }
    output << name << "\n";
}

/*******************************************************************
 * Antenna API
 ******************************************************************/

std::vector<std::string> SoapySDRPlay::listAntennas(const int direction, const size_t channel) const
{
    std::vector<std::string> antennas;

    if (direction == SOAPY_SDR_TX) {
        return antennas;
    }

    if (device.hwVer == SDRPLAY_RSP1_ID || device.hwVer == SDRPLAY_RSP1A_ID || device.hwVer == SDRPLAY_RSP1B_ID) {
        antennas.push_back("RX");
    }
    else if (device.hwVer == SDRPLAY_RSP2_ID) {
        antennas.push_back("Antenna A");
        antennas.push_back("Antenna B");
        antennas.push_back("Hi-Z");
    }
    else if (device.hwVer == SDRPLAY_RSPdx_ID) {
        antennas.push_back("Antenna A");
        antennas.push_back("Antenna B");
        antennas.push_back("Antenna C");
    }
    else if (device.hwVer == SDRPLAY_RSPdxR2_ID) {
        antennas.push_back("Antenna A");
        antennas.push_back("Antenna B");
        antennas.push_back("Antenna C");
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        if (device.rspDuoMode == sdrplay_api_RspDuoMode_Single_Tuner ||
            device.rspDuoMode == sdrplay_api_RspDuoMode_Master) {
            antennas.push_back("Tuner 1 50 ohm");
            antennas.push_back("Tuner 1 Hi-Z");
            antennas.push_back("Tuner 2 50 ohm");
        }
        else if (device.rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner) {
            if (channel == 0) {
                // No Hi-Z antenna in Dual Tuner mode
                // For diversity reception you would want the two tuner inputs
                // to be the same otherwise there is a mismatch in the gain
                // control.
                antennas.push_back("Tuner 1 50 ohm");
            }
            else if (channel == 1) {
                antennas.push_back("Tuner 2 50 ohm");
            }
        }
        else if (device.rspDuoMode == sdrplay_api_RspDuoMode_Slave) {
            if (device.tuner == sdrplay_api_Tuner_A) {
                antennas.push_back("Tuner 1 50 ohm");
                antennas.push_back("Tuner 1 Hi-Z");
            }
            else if (device.tuner == sdrplay_api_Tuner_B) {
                antennas.push_back("Tuner 2 50 ohm");
            }
        }
    }
    return antennas;
}

void SoapySDRPlay::setAntenna(const int direction, const size_t channel, const std::string &name)
{
    // Check direction
    if ((direction != SOAPY_SDR_RX) || (device.hwVer == SDRPLAY_RSP1_ID) || (device.hwVer == SDRPLAY_RSP1A_ID) || (device.hwVer == SDRPLAY_RSP1B_ID)) {
        return;
    }

    std::string persistName;
    bool shouldPersist = false;

    {
        std::lock_guard <std::mutex> lock(_general_state_mutex);

        if (device.hwVer == SDRPLAY_RSP2_ID)
    {
        bool changeToAntennaA_B = false;

        if (name == "Antenna A")
        {
            chParams->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_A;
            changeToAntennaA_B = true;
        }
        else if (name == "Antenna B")
        {
            chParams->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_B;
            changeToAntennaA_B = true;
        }
        else if (name == "Hi-Z")
        {
            chParams->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_1;

            if (streamActive)
            {
                sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AmPortSelect, sdrplay_api_Update_Ext1_None);
                if (err != sdrplay_api_Success)
                {
                    SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(Rsp2_AmPortSelect) failed: %s", sdrplay_api_GetErrorString(err));
                }
            }
        }
        else
        {
            return;
        }

        persistName = name;
        shouldPersist = true;

        if (changeToAntennaA_B)
        {

            //if we are currently High_Z, make the switch first.
            if (chParams->rsp2TunerParams.amPortSel == sdrplay_api_Rsp2_AMPORT_1)
            {
                chParams->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_2;

                if (streamActive)
                {
                    sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AmPortSelect, sdrplay_api_Update_Ext1_None);
                    if (err != sdrplay_api_Success)
                    {
                        SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(Rsp2_AmPortSelect) failed: %s", sdrplay_api_GetErrorString(err));
                    }
                }
            }
            else
            {
                if (streamActive)
                {
                    sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AntennaControl, sdrplay_api_Update_Ext1_None);
                    if (err != sdrplay_api_Success)
                    {
                        SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(Rsp2_AntennaControl) failed: %s", sdrplay_api_GetErrorString(err));
                    }
                }
            }
        }
    }
    else if (device.hwVer == SDRPLAY_RSPdx_ID)
    {
        if (!deviceParams->devParams)
        {
            SoapySDR_log(SOAPY_SDR_WARNING, "setAntenna: devParams is null for RSPdx");
            return;
        }
        if (name == "Antenna A")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_A;
        }
        else if (name == "Antenna B")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_B;
        }
        else if (name == "Antenna C")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_C;
        }
        else
        {
            return;
        }

        persistName = name;
        shouldPersist = true;

        if (streamActive)
        {
            sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_AntennaControl);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(RspDx_AntennaControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
        }
    }
    else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
    {
        if (!deviceParams->devParams)
        {
            SoapySDR_log(SOAPY_SDR_WARNING, "setAntenna: devParams is null for RSPdxR2");
            return;
        }
        if (name == "Antenna A")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_A;
        }
        else if (name == "Antenna B")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_B;
        }
        else if (name == "Antenna C")
        {
            deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_C;
        }
        else
        {
            return;
        }

        persistName = name;
        shouldPersist = true;

        if (streamActive)
        {
            sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_AntennaControl);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(RspDx_AntennaControl) failed: %s", sdrplay_api_GetErrorString(err));
            }
        }
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
        bool changeToTunerA_B = false;
        bool changeAmPort = false;
        bool isTunerChangeAllowed = device.rspDuoMode == sdrplay_api_RspDuoMode_Single_Tuner || device.rspDuoMode == sdrplay_api_RspDuoMode_Master;
        bool applied = true;

        if (name == "Tuner 1 50 ohm")
        {
            changeAmPort = chParams->rspDuoTunerParams.tuner1AmPortSel != sdrplay_api_RspDuo_AMPORT_2;
            chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
            changeToTunerA_B = isTunerChangeAllowed && device.tuner != sdrplay_api_Tuner_A;
        }
        else if (name == "Tuner 2 50 ohm")
        {
            changeAmPort = chParams->rspDuoTunerParams.tuner1AmPortSel != sdrplay_api_RspDuo_AMPORT_2;
            changeToTunerA_B = isTunerChangeAllowed && device.tuner != sdrplay_api_Tuner_B;
        }
        else if (name == "Tuner 1 Hi-Z")
        {
            changeAmPort = chParams->rspDuoTunerParams.tuner1AmPortSel != sdrplay_api_RspDuo_AMPORT_1;
            chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_1;
            changeToTunerA_B = isTunerChangeAllowed && device.tuner != sdrplay_api_Tuner_A;
        }
        else
        {
            return;
        }

        if (!changeToTunerA_B)
        {
            if (changeAmPort)
            {
                //if we are currently High_Z, make the switch first.
                if (streamActive)
                {
                    sdrplay_api_ErrT err = updateLocked(device.dev, device.tuner, sdrplay_api_Update_RspDuo_AmPortSelect, sdrplay_api_Update_Ext1_None);
                    if (err != sdrplay_api_Success)
                    {
                        SoapySDR_logf(SOAPY_SDR_WARNING, "updateLocked(RspDuo_AmPortSelect) failed: %s", sdrplay_api_GetErrorString(err));
                    }
                }
            }
        }
        else
        {
            if (streamActive)
            {
                if (device.rspDuoMode == sdrplay_api_RspDuoMode_Single_Tuner)
                {
                    sdrplay_api_ErrT err;
                    {
                        SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
                        err = sdrplay_api_SwapRspDuoActiveTuner(device.dev,
                                   &device.tuner, chParams->rspDuoTunerParams.tuner1AmPortSel);
                    }
                    if (err != sdrplay_api_Success)
                    {
                        SoapySDR_logf(SOAPY_SDR_WARNING, "SwapRspDuoActiveTuner Error: %s", sdrplay_api_GetErrorString(err));
                        applied = false;
                    }
                    chParams = device.tuner == sdrplay_api_Tuner_B ?
                               deviceParams->rxChannelB : deviceParams->rxChannelA;
                }
                else if (device.rspDuoMode == sdrplay_api_RspDuoMode_Master)
                {
                    // not sure what is the best way to handle this case - fv
                    SoapySDR_log(SOAPY_SDR_WARNING, "tuner change not allowed in RSPduo Master mode while the device is streaming");
                    applied = false;
                }
            }
            else
            {
                // preserve all the device and tuner settings
                // when changing tuner/antenna
                if (!deviceParams->devParams)
                {
                    SoapySDR_log(SOAPY_SDR_WARNING, "setAntenna: devParams is null for RSPduo tuner switch");
                    return;
                }
                sdrplay_api_DevParamsT devParams = *deviceParams->devParams;
                sdrplay_api_RxChannelParamsT rxChannelParams = *chParams;
                sdrplay_api_TunerSelectT other_tuner = (device.tuner == sdrplay_api_Tuner_A) ? sdrplay_api_Tuner_B : sdrplay_api_Tuner_A;
                selectDevice(other_tuner, device.rspDuoMode,
                             device.rspDuoSampleFreq, nullptr);
                // restore device and tuner settings
                *deviceParams->devParams = devParams;
                *chParams = rxChannelParams;
            }
        }
        if (applied)
        {
            persistName = name;
            shouldPersist = true;
        }
    }
    }

    if (shouldPersist)
    {
        const std::string key = cacheKey.empty() ? serNo : cacheKey;
        savePersistedAntenna(key, channel, persistName);
    }
}

std::string SoapySDRPlay::getAntenna(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (direction == SOAPY_SDR_TX)
    {
        return "";
    }

    if (device.hwVer == SDRPLAY_RSP2_ID)
    {
        if (chParams->rsp2TunerParams.amPortSel == sdrplay_api_Rsp2_AMPORT_1) {
            return "Hi-Z";
        }
        else if (chParams->rsp2TunerParams.antennaSel == sdrplay_api_Rsp2_ANTENNA_A) {
            return "Antenna A";
        }
        else {
            return "Antenna B";
        }
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
        if (device.tuner == sdrplay_api_Tuner_A ||
                (device.tuner == sdrplay_api_Tuner_Both && channel == 0)) {
            if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1) {
                return "Tuner 1 Hi-Z";
            } else {
                return "Tuner 1 50 ohm";
            }
        } else if (device.tuner == sdrplay_api_Tuner_B ||
                  (device.tuner == sdrplay_api_Tuner_Both && channel == 1)) {
                return "Tuner 2 50 ohm";
        }
    }
    else if (device.hwVer == SDRPLAY_RSPdx_ID)
    {
        if (!deviceParams->devParams) {
            return "RX";
        }
        if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_A) {
            return "Antenna A";
        }
        else if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_B) {
            return "Antenna B";
        }
        else if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_C) {
            return "Antenna C";
        }
    }
    else if (device.hwVer == SDRPLAY_RSPdxR2_ID)
    {
        if (!deviceParams->devParams) {
            return "RX";
        }
        if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_A) {
            return "Antenna A";
        }
        else if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_B) {
            return "Antenna B";
        }
        else if (deviceParams->devParams->rspDxParams.antennaSel == sdrplay_api_RspDx_ANTENNA_C) {
            return "Antenna C";
        }
    }

    return "RX";
}

void SoapySDRPlay::setAntennaPersistent(const int direction, const size_t channel,
                                        const std::string &name, const bool persistent)
{
    if (direction != SOAPY_SDR_RX || channel > 1)
    {
        return;
    }

    // Store persistence settings
    {
        std::lock_guard<std::mutex> lock(_general_state_mutex);
        antennaPersistentEnabled[channel] = persistent;
        if (persistent)
        {
            persistentAntennaName[channel] = name;
        }
        else
        {
            persistentAntennaName[channel].clear();
        }
    }

    // Apply the antenna setting immediately
    setAntenna(direction, channel, name);
}

bool SoapySDRPlay::getAntennaPersistent(const int direction, const size_t channel) const
{
    if (direction != SOAPY_SDR_RX || channel > 1)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(_general_state_mutex);
    return antennaPersistentEnabled[channel];
}
