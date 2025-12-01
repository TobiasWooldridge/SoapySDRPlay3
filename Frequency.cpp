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
 * Frequency API
 ******************************************************************/

void SoapySDRPlay::setFrequency(const int direction,
                                 const size_t channel,
                                 const double frequency,
                                 const SoapySDR::Kwargs &args)
{
    // default to RF
    setFrequency(direction, channel, "RF", frequency, args);
}

void SoapySDRPlay::setFrequency(const int direction,
                                 const size_t channel,
                                 const std::string &name,
                                 const double frequency,
                                 const SoapySDR::Kwargs &args)
{
   std::lock_guard <std::mutex> lock(_general_state_mutex);


   if (direction == SOAPY_SDR_RX)
   {
      if (name == "RF")
      {
         SoapySDR::RangeList frequencyRange = getFrequencyRange(direction, channel, name);
         if (!(frequency >= frequencyRange.front().minimum() && frequency <= frequencyRange.back().maximum()))
         {
            SoapySDR_logf(SOAPY_SDR_WARNING, "RF center frequency out of range - frequency=%lg", frequency);
            return;
         }
         if (chParams->tunerParams.rfFreq.rfHz != (uint32_t)frequency)
         {
            chParams->tunerParams.rfFreq.rfHz = (uint32_t)frequency;
            if (streamActive)
            {
               executeApiUpdate(sdrplay_api_Update_Tuner_Frf, sdrplay_api_Update_Ext1_None,
                                &rf_changed, "Tuner_Frf");
            }
         }
      }
      // can't set ppm for RSPduo slaves
      else if ((name == "CORR") && deviceParams->devParams &&
              (deviceParams->devParams->ppm != frequency))
      {
         deviceParams->devParams->ppm = frequency;
         if (streamActive)
         {
            executeApiUpdate(sdrplay_api_Update_Dev_Ppm, sdrplay_api_Update_Ext1_None,
                             nullptr, "Dev_Ppm");
         }
      }
   }
}

double SoapySDRPlay::getFrequency(const int direction, const size_t channel) const
{
    // default to RF
    return getFrequency(direction, channel, "RF");
}

double SoapySDRPlay::getFrequency(const int direction, const size_t channel, const std::string &name) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (name == "RF")
    {
        return static_cast<double>(chParams->tunerParams.rfFreq.rfHz);
    }
    else if (name == "CORR")
    {
        if (deviceParams->devParams)
        {
            return deviceParams->devParams->ppm;
        } else {
            return 0;
        }
    }

    return 0;
}

std::vector<std::string> SoapySDRPlay::listFrequencies(const int direction, const size_t channel) const
{
    std::vector<std::string> names;
    names.push_back("RF");
    names.push_back("CORR");
    return names;
}

SoapySDR::RangeList SoapySDRPlay::getFrequencyRange(const int direction, const size_t channel) const
{
    return getFrequencyRange(direction, channel, "RF");
}

SoapySDR::RangeList SoapySDRPlay::getFrequencyRange(const int direction, const size_t channel, const std::string &name) const
{
    SoapySDR::RangeList results;
    if (name == "RF")
    {
        if(device.hwVer == SDRPLAY_RSP1_ID)
        {
            results.push_back(SoapySDR::Range(10000, 2000000000));
        }
        else
        {
            results.push_back(SoapySDR::Range(1000, 2000000000));
        }
    }
    return results;
}

SoapySDR::ArgInfoList SoapySDRPlay::getFrequencyArgsInfo(const int direction, const size_t channel) const
{
    SoapySDR::ArgInfoList freqArgs;

    return freqArgs;
}
