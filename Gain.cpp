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
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP1_ID))
   {
      return SoapySDR::Range(0, 3);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP2_ID))
   {
      return SoapySDR::Range(0, 8);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSPduo_ID))
   {
      return SoapySDR::Range(0, 9);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP1A_ID))
   {
      return SoapySDR::Range(0, 9);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP1B_ID))
   {
      return SoapySDR::Range(0, 9);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSPdx_ID))
   {
      return SoapySDR::Range(0, 27);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSPdxR2_ID))
   {
      return SoapySDR::Range(0, 27);
   }
    return SoapySDR::Range(20, 59);
}

/*******************************************************************
 * Overall Gain API (distributes gain across LNA and IF stages)
 ******************************************************************/

void SoapySDRPlay::setGain(const int direction, const size_t channel, const double value)
{
    std::lock_guard<std::mutex> lock(_general_state_mutex);

    // Map total gain (0-66 dB) to LNAstate and IFGR
    // Strategy: Maximize LNA gain first (lower LNAstate = less attenuation = more gain)
    // Then adjust IFGR for fine tuning
    //
    // For RSPdx/RSPdx-R2:
    //   - LNAstate 0 = minimum attenuation = maximum RF gain (~27 dB)
    //   - LNAstate 27 = maximum attenuation = minimum RF gain (~0 dB)
    //   - IFGR 20 = minimum reduction = maximum IF gain (~39 dB)
    //   - IFGR 59 = maximum reduction = minimum IF gain (~0 dB)
    //
    // Total gain range: 0-66 dB (27 dB from LNA + 39 dB from IF)

    int lna_state, if_gr;

    // Get max LNA states for this device
    int max_lna_state = 27;  // Default for RSPdx
    if (device.hwVer == SDRPLAY_RSP1_ID) max_lna_state = 3;
    else if (device.hwVer == SDRPLAY_RSP2_ID) max_lna_state = 8;
    else if (device.hwVer == SDRPLAY_RSPduo_ID) max_lna_state = 9;
    else if (device.hwVer == SDRPLAY_RSP1A_ID) max_lna_state = 9;
    else if (device.hwVer == SDRPLAY_RSP1B_ID) max_lna_state = 9;

    // Calculate LNA gain equivalent (approximate dB per state)
    double lna_db_per_state = 27.0 / max_lna_state;

    if (value >= 47) {
        // High gain: Max LNA, adjust IFGR
        lna_state = 0;  // Max LNA gain
        if_gr = 20 + static_cast<int>(66 - value);  // Scale IFGR: value=66->IFGR=20, value=47->IFGR=39
    } else if (value >= 20) {
        // Medium gain: Reduce LNA, IFGR at minimum reduction
        lna_state = static_cast<int>((47 - value) / lna_db_per_state);
        if_gr = 20;  // Max IF gain (min reduction)
    } else {
        // Low gain: Min LNA, increase IFGR
        lna_state = max_lna_state;
        if_gr = 20 + static_cast<int>(20 - value);
    }

    // Clamp values to valid ranges
    lna_state = std::max(0, std::min(max_lna_state, lna_state));
    if_gr = std::max(20, std::min(59, if_gr));

    // Update tracked values
    desired_lna_state = lna_state;
    desired_if_gr = if_gr;

    // Apply to device
    chParams->tunerParams.gain.LNAstate = lna_state;
    if (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_DISABLE) {
        chParams->tunerParams.gain.gRdB = if_gr;
    }

    SoapySDR_logf(SOAPY_SDR_DEBUG, "setGain(%.1f dB) -> LNAstate=%d, gRdB=%d (AGC=%s)",
                  value, lna_state, if_gr,
                  (chParams->ctrlParams.agc.enable != sdrplay_api_AGC_DISABLE) ? "on" : "off");

    if (streamActive) {
        executeApiUpdate(sdrplay_api_Update_Tuner_Gr, sdrplay_api_Update_Ext1_None,
                         &gr_changed, "Tuner_Gr");
    }
}

double SoapySDRPlay::getGain(const int direction, const size_t channel) const
{
    std::lock_guard<std::mutex> lock(_general_state_mutex);

    // Calculate approximate total gain from LNAstate and gRdB
    int lna_state = chParams->tunerParams.gain.LNAstate;
    int if_gr = chParams->tunerParams.gain.gRdB;

    // Get max LNA states for this device
    int max_lna_state = 27;  // Default for RSPdx
    if (device.hwVer == SDRPLAY_RSP1_ID) max_lna_state = 3;
    else if (device.hwVer == SDRPLAY_RSP2_ID) max_lna_state = 8;
    else if (device.hwVer == SDRPLAY_RSPduo_ID) max_lna_state = 9;
    else if (device.hwVer == SDRPLAY_RSP1A_ID) max_lna_state = 9;
    else if (device.hwVer == SDRPLAY_RSP1B_ID) max_lna_state = 9;

    // Approximate: LNAstate 0 = ~27 dB, LNAstate max = ~0 dB
    double lna_gain = 27.0 * (max_lna_state - lna_state) / max_lna_state;
    // IFGR 20 = ~39 dB, IFGR 59 = ~0 dB
    double if_gain = 39.0 * (59 - if_gr) / 39.0;

    return lna_gain + if_gain;
}

SoapySDR::Range SoapySDRPlay::getGainRange(const int direction, const size_t channel) const
{
    // Total gain range: 0 to 66 dB (approximately)
    // This combines LNA gain (~27 dB for RSPdx) + IF gain (~39 dB)
    return SoapySDR::Range(0, 66);
}
