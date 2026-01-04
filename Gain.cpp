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
#include <vector>

/*******************************************************************
 * LNA Gain Reduction Tables (from gr-osmosdr/SDRplay documentation)
 *
 * These tables contain the cumulative gain reduction (in dB) for each
 * LNA state at different frequency bands. The values are device-specific
 * and frequency-dependent.
 *
 * Format: rfGRs[lna_state] = total gain reduction in dB
 ******************************************************************/

// Get LNA gain reduction values for the current device and frequency
static std::vector<int> getLnaGainReductions(unsigned char hwVer, double rfHz, const std::string& antenna = "")
{
    if (hwVer == SDRPLAY_RSP1_ID)
    {
        if (rfHz <= 420e6)
            return { 0, 24, 19, 43 };
        else if (rfHz <= 1000e6)
            return { 0, 7, 19, 26 };
        else // <= 2000e6
            return { 0, 5, 19, 24 };
    }
    else if (hwVer == SDRPLAY_RSP1A_ID || hwVer == SDRPLAY_RSP1B_ID)
    {
        if (rfHz <= 60e6)
            return { 0, 6, 12, 18, 37, 42, 61 };
        else if (rfHz <= 420e6)
            return { 0, 6, 12, 18, 20, 26, 32, 38, 57, 62 };
        else if (rfHz <= 1000e6)
            return { 0, 7, 13, 19, 20, 27, 33, 39, 45, 64 };
        else // <= 2000e6
            return { 0, 6, 12, 20, 26, 32, 38, 43, 62 };
    }
    else if (hwVer == SDRPLAY_RSP2_ID)
    {
        if (rfHz <= 60e6 && antenna == "Hi-Z")
            return { 0, 6, 12, 18, 37 };
        else if (rfHz <= 420e6)
            return { 0, 10, 15, 21, 24, 34, 39, 45, 64 };
        else if (rfHz <= 1000e6)
            return { 0, 7, 10, 17, 22, 41 };
        else // <= 2000e6
            return { 0, 5, 21, 15, 15, 34 };
    }
    else if (hwVer == SDRPLAY_RSPduo_ID)
    {
        if (rfHz <= 60e6 && antenna == "Tuner 1 Hi-Z")
            return { 0, 6, 12, 18, 37 };
        else if (rfHz <= 60e6)
            return { 0, 6, 12, 18, 37, 42, 61 };
        else if (rfHz <= 420e6)
            return { 0, 6, 12, 18, 20, 26, 32, 38, 57, 62 };
        else if (rfHz <= 1000e6)
            return { 0, 7, 13, 19, 20, 27, 33, 39, 45, 64 };
        else // <= 2000e6
            return { 0, 6, 12, 20, 26, 32, 38, 43, 62 };
    }
    else if (hwVer == SDRPLAY_RSPdx_ID || hwVer == SDRPLAY_RSPdxR2_ID)
    {
        if (rfHz <= 2e6)  // HDR mode
            return { 0, 3, 6, 9, 12, 15, 18, 21, 24, 25, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57, 60 };
        else if (rfHz <= 12e6)
            return { 0, 3, 6, 9, 12, 15, 24, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57, 60 };
        else if (rfHz <= 60e6)
            return { 0, 3, 6, 9, 12, 15, 18, 24, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57, 60 };
        else if (rfHz <= 250e6)
            return { 0, 3, 6, 9, 12, 15, 24, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84 };
        else if (rfHz <= 420e6)
            return { 0, 3, 6, 9, 12, 15, 18, 24, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84 };
        else if (rfHz <= 1000e6)
            return { 0, 7, 10, 13, 16, 19, 22, 25, 31, 34, 37, 40, 43, 46, 49, 52, 55, 58, 61, 64, 67 };
        else // <= 2000e6
            return { 0, 5, 8, 11, 14, 17, 20, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65 };
    }

    // Default fallback
    return { 0 };
}

// Get the maximum LNA state for the current device and frequency
static int getMaxLnaState(unsigned char hwVer, double rfHz, const std::string& antenna = "")
{
    auto reductions = getLnaGainReductions(hwVer, rfHz, antenna);
    return static_cast<int>(reductions.size()) - 1;
}

// Get the total gain reduction in dB for a given LNA state
static int getLnaGainReduction(unsigned char hwVer, double rfHz, int lnaState, const std::string& antenna = "")
{
    auto reductions = getLnaGainReductions(hwVer, rfHz, antenna);
    if (lnaState < 0 || lnaState >= static_cast<int>(reductions.size()))
        return reductions.back();  // Return max reduction if out of range
    return reductions[lnaState];
}

/*******************************************************************
 * Frontend corrections API
 ******************************************************************/

bool SoapySDRPlay::hasDCOffsetMode(const int direction, const size_t channel) const
{
    return true;
}

void SoapySDRPlay::setDCOffsetMode(const int direction, const size_t channel, const bool automatic)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    //enable/disable automatic DC removal
    chParams->ctrlParams.dcOffset.DCenable = static_cast<unsigned char>(automatic);
    chParams->ctrlParams.dcOffset.IQenable = static_cast<unsigned char>(automatic);
}

bool SoapySDRPlay::getDCOffsetMode(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    return static_cast<bool>(chParams->ctrlParams.dcOffset.DCenable);
}

bool SoapySDRPlay::hasDCOffset(const int direction, const size_t channel) const
{
    //is a specific DC removal value configurable?
    return false;
}

bool SoapySDRPlay::hasFrequencyCorrection(const int direction, const size_t channel) const {
    return true;
}

void SoapySDRPlay::setFrequencyCorrection(const int direction, const size_t channel, const double value) {
    setFrequency(direction, channel, "CORR", value);
}

double SoapySDRPlay::getFrequencyCorrection(const int direction, const size_t channel) const {
    return getFrequency(direction, channel, "CORR");
}

/*******************************************************************
 * Gain API
 ******************************************************************/

std::vector<std::string> SoapySDRPlay::listGains(const int direction, const size_t channel) const
{
    //list available gain elements,
    //the functions below have a "name" parameter
    std::vector<std::string> results;

    results.push_back("IFGR");
    results.push_back("RFGR");

    return results;
}

bool SoapySDRPlay::hasGainMode(const int direction, const size_t channel) const
{
    return true;
}

void SoapySDRPlay::setGainMode(const int direction, const size_t channel, const bool automatic)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    sdrplay_api_AgcControlT agc_control = automatic ? sdrplay_api_AGC_CTRL_EN : sdrplay_api_AGC_DISABLE;
    if (chParams->ctrlParams.agc.enable != agc_control)
    {
        chParams->ctrlParams.agc.enable = agc_control;
        if (streamActive)
        {
            executeApiUpdate(sdrplay_api_Update_Ctrl_Agc, sdrplay_api_Update_Ext1_None,
                             nullptr, "Ctrl_Agc");
        }
    }
}

bool SoapySDRPlay::getGainMode(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    return chParams->ctrlParams.agc.enable != sdrplay_api_AGC_DISABLE;
}

void SoapySDRPlay::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   bool doUpdate = false;

   if (name == "IFGR")
   {
      if (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_DISABLE)
      {
         // Update tracked value
         desired_if_gr = static_cast<int>(value);

         //apply the change if the required value is different from gRdB
         if (chParams->tunerParams.gain.gRdB != static_cast<int>(value))
         {
            chParams->tunerParams.gain.gRdB = static_cast<int>(value);
            doUpdate = true;
         }
      }
      else
      {
         SoapySDR_log(SOAPY_SDR_WARNING, "Not updating IFGR gain because AGC is enabled");
      }
   }
   else if (name == "RFGR")
   {
      // Update tracked value
      desired_lna_state = static_cast<int>(value);

      if (chParams->tunerParams.gain.LNAstate != static_cast<int>(value)) {
          chParams->tunerParams.gain.LNAstate = static_cast<int>(value);
          doUpdate = true;
      }
   }

   // Log the gain change for debugging
   if (doUpdate) {
      SoapySDR_logf(SOAPY_SDR_DEBUG, "setGain(%s, %.1f) -> LNAstate=%d, gRdB=%d",
                    name.c_str(), value,
                    chParams->tunerParams.gain.LNAstate,
                    chParams->tunerParams.gain.gRdB);
   }

   if ((doUpdate == true) && (streamActive))
   {
      executeApiUpdate(sdrplay_api_Update_Tuner_Gr, sdrplay_api_Update_Ext1_None,
                       &gr_changed, "Tuner_Gr");
   }
}

double SoapySDRPlay::getGain(const int direction, const size_t channel, const std::string &name) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (name == "IFGR")
   {
       return chParams->tunerParams.gain.gRdB;
   }
   else if (name == "RFGR")
   {
      return chParams->tunerParams.gain.LNAstate;
   }

   return 0;
}

SoapySDR::Range SoapySDRPlay::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
   if (name == "IFGR")
   {
      return SoapySDR::Range(20, 59);
   }
   else if (name == "RFGR")
   {
      // Use frequency-dependent max LNA state
      double rfHz = chParams->tunerParams.rfFreq.rfHz;
      int maxState = getMaxLnaState(device.hwVer, rfHz);
      return SoapySDR::Range(0, maxState);
   }
   return SoapySDR::Range(20, 59);
}

/*******************************************************************
 * Overall Gain API (distributes gain across LNA and IF stages)
 ******************************************************************/

void SoapySDRPlay::setGain(const int direction, const size_t channel, const double value)
{
    std::lock_guard<std::mutex> lock(_general_state_mutex);

    // Map total gain to LNAstate and IFGR using proper gain tables
    // Strategy: Find the LNA state that gives closest gain, then fine-tune with IFGR
    //
    // LNAstate 0 = minimum attenuation = maximum RF gain
    // Higher LNAstate = more attenuation = less RF gain
    // IFGR 20 = minimum reduction = maximum IF gain (~39 dB)
    // IFGR 59 = maximum reduction = minimum IF gain (~0 dB)

    double rfHz = chParams->tunerParams.rfFreq.rfHz;
    auto lnaReductions = getLnaGainReductions(device.hwVer, rfHz);
    int maxLnaState = static_cast<int>(lnaReductions.size()) - 1;

    // Max possible gain: LNA at state 0 (0 dB reduction) + IFGR at 20 (39 dB gain)
    // The LNA reduction values are cumulative, so max LNA gain = maxReduction - 0 = maxReduction
    int maxLnaReduction = lnaReductions.back();
    double maxGain = static_cast<double>(maxLnaReduction) + 39.0;  // Max LNA gain + max IF gain

    // Clamp requested gain to valid range
    double targetGain = std::max(0.0, std::min(maxGain, value));

    // Find the best LNA state by finding the reduction value closest to what we need
    // We want: lnaGain + ifGain = targetGain
    // lnaGain = maxLnaReduction - lnaReductions[state]
    // ifGain = 59 - if_gr (range 0-39 dB)
    int bestLnaState = 0;
    int bestIfGr = 20;
    double bestError = maxGain;

    for (int state = 0; state <= maxLnaState; state++)
    {
        double lnaGain = static_cast<double>(maxLnaReduction - lnaReductions[state]);
        double neededIfGain = targetGain - lnaGain;

        // IF gain range is 0-39 dB (IFGR 59-20)
        if (neededIfGain < 0) neededIfGain = 0;
        if (neededIfGain > 39) neededIfGain = 39;

        int ifGr = 59 - static_cast<int>(neededIfGain);
        double actualIfGain = 59.0 - ifGr;
        double actualGain = lnaGain + actualIfGain;
        double error = std::abs(actualGain - targetGain);

        if (error < bestError)
        {
            bestError = error;
            bestLnaState = state;
            bestIfGr = ifGr;
        }
    }

    // Update tracked values
    desired_lna_state = bestLnaState;
    desired_if_gr = bestIfGr;

    // Apply to device
    chParams->tunerParams.gain.LNAstate = bestLnaState;
    if (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_DISABLE) {
        chParams->tunerParams.gain.gRdB = bestIfGr;
    }

    double actualLnaGain = static_cast<double>(maxLnaReduction - lnaReductions[bestLnaState]);
    double actualIfGain = 59.0 - bestIfGr;
    SoapySDR_logf(SOAPY_SDR_DEBUG, "setGain(%.1f dB) -> LNAstate=%d (%.1f dB), gRdB=%d (%.1f dB), total=%.1f dB (AGC=%s)",
                  value, bestLnaState, actualLnaGain, bestIfGr, actualIfGain,
                  actualLnaGain + actualIfGain,
                  (chParams->ctrlParams.agc.enable != sdrplay_api_AGC_DISABLE) ? "on" : "off");

    if (streamActive) {
        executeApiUpdate(sdrplay_api_Update_Tuner_Gr, sdrplay_api_Update_Ext1_None,
                         &gr_changed, "Tuner_Gr");
    }
}

double SoapySDRPlay::getGain(const int direction, const size_t channel) const
{
    std::lock_guard<std::mutex> lock(_general_state_mutex);

    // Calculate total gain from LNAstate and gRdB using proper gain tables
    int lnaState = chParams->tunerParams.gain.LNAstate;
    int ifGr = chParams->tunerParams.gain.gRdB;
    double rfHz = chParams->tunerParams.rfFreq.rfHz;

    auto lnaReductions = getLnaGainReductions(device.hwVer, rfHz);
    int maxLnaReduction = lnaReductions.back();

    // LNA gain = max reduction - current reduction
    int lnaReduction = getLnaGainReduction(device.hwVer, rfHz, lnaState);
    double lnaGain = static_cast<double>(maxLnaReduction - lnaReduction);

    // IF gain = 59 - gRdB (range 0-39 dB)
    double ifGain = 59.0 - ifGr;
    if (ifGain < 0) ifGain = 0;
    if (ifGain > 39) ifGain = 39;

    return lnaGain + ifGain;
}

SoapySDR::Range SoapySDRPlay::getGainRange(const int direction, const size_t channel) const
{
    // Total gain range depends on device and frequency
    // Max gain = max LNA gain (from tables) + max IF gain (39 dB)
    double rfHz = chParams->tunerParams.rfFreq.rfHz;
    auto lnaReductions = getLnaGainReductions(device.hwVer, rfHz);
    int maxLnaReduction = lnaReductions.back();
    double maxGain = static_cast<double>(maxLnaReduction) + 39.0;
    return SoapySDR::Range(0, maxGain);
}
