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

/*******************************************************************
 * Identification API
 ******************************************************************/

std::string SoapySDRPlay::getDriverKey(void) const
{
    return "SDRplay";
}

std::string SoapySDRPlay::getHardwareKey(void) const
{
    if (hwVer == SDRPLAY_RSP1_ID) return "RSP1";
    if (hwVer == SDRPLAY_RSP1A_ID) return "RSP1A";
    if (hwVer == SDRPLAY_RSP1B_ID) return "RSP1B";
    if (hwVer == SDRPLAY_RSP2_ID) return "RSP2";
    if (hwVer == SDRPLAY_RSPduo_ID) return "RSPduo";
    if (hwVer == SDRPLAY_RSPdx_ID) return "RSPdx";
    if (hwVer == SDRPLAY_RSPdxR2_ID) return "RSPdx-R2";
    return "UNKNOWN";
}

SoapySDR::Kwargs SoapySDRPlay::getHardwareInfo(void) const
{
    // key/value pairs for any useful information
    // this also gets printed in --probe
    SoapySDR::Kwargs hwArgs;

    float ver = SoapySDRPlay::sdrplay_api::get_version();
    hwArgs["sdrplay_api_api_version"] = std::to_string(ver);
    hwArgs["sdrplay_api_hw_version"] = std::to_string(device.hwVer);

    return hwArgs;
}

/*******************************************************************
 * Channels API
 ******************************************************************/

size_t SoapySDRPlay::getNumChannels(const int dir) const
{
    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner) {
        return (dir == SOAPY_SDR_RX) ? 2 : 0;
    }
    return (dir == SOAPY_SDR_RX) ? 1 : 0;
}
