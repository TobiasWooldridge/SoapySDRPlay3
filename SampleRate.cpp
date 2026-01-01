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
 * Sample Rate API
 ******************************************************************/

/* input_sample_rate:  sample rate used by the SDR
 * output_sample_rate: sample rate as seen by the client app
 *                     (<= input_sample_rate because of decimation)
 */

void SoapySDRPlay::setSampleRate(const int direction, const size_t channel, const double output_sample_rate)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    SoapySDR_logf(SOAPY_SDR_DEBUG, "Requested output sample rate: %lf", output_sample_rate);

    if (direction == SOAPY_SDR_RX)
    {
       unsigned int decM;
       unsigned int decEnable;
       sdrplay_api_If_kHzT ifType;
       double input_sample_rate = getInputSampleRateAndDecimation(output_sample_rate, &decM, &decEnable, &ifType);
       if (input_sample_rate < 0) {
           SoapySDR_logf(SOAPY_SDR_WARNING, "invalid sample rate. Sample rate unchanged.");
           return;
       }

       sdrplay_api_Bw_MHzT bwType = getBwEnumForRate(output_sample_rate);

       sdrplay_api_ReasonForUpdateT reasonForUpdate = sdrplay_api_Update_None;
       bool waitForUpdate = false;
       if (deviceParams->devParams && input_sample_rate != deviceParams->devParams->fsFreq.fsHz)
       {
          deviceParams->devParams->fsFreq.fsHz = input_sample_rate;
          reasonForUpdate = static_cast<sdrplay_api_ReasonForUpdateT>(reasonForUpdate | sdrplay_api_Update_Dev_Fs);
          waitForUpdate = true;
       }
       if (ifType != chParams->tunerParams.ifType)
       {
          chParams->tunerParams.ifType = ifType;
          reasonForUpdate = static_cast<sdrplay_api_ReasonForUpdateT>(reasonForUpdate | sdrplay_api_Update_Tuner_IfType);
       }
       if (decM != chParams->ctrlParams.decimation.decimationFactor)
       {
          chParams->ctrlParams.decimation.enable = decEnable;
          chParams->ctrlParams.decimation.decimationFactor = decM;
          if (ifType == sdrplay_api_IF_Zero) {
              chParams->ctrlParams.decimation.wideBandSignal = 1;
          }
          else {
              chParams->ctrlParams.decimation.wideBandSignal = 0;
          }
          // Update cached buffer threshold to avoid division in hot path
          cachedBufferThreshold = (decM > 0) ? (bufferLength / decM) : bufferLength.load();
          reasonForUpdate = static_cast<sdrplay_api_ReasonForUpdateT>(reasonForUpdate | sdrplay_api_Update_Ctrl_Decimation);
       }
       if (bwType != chParams->tunerParams.bwType)
       {
          chParams->tunerParams.bwType = bwType;
          reasonForUpdate = static_cast<sdrplay_api_ReasonForUpdateT>(reasonForUpdate | sdrplay_api_Update_Tuner_BwType);
       }
       if (reasonForUpdate != sdrplay_api_Update_None)
       {
          {
             std::lock_guard<std::mutex> lock(_streams_mutex);
             if (_streams[0]) { _streams[0]->reset = true; }
             if (_streams[1]) { _streams[1]->reset = true; }
          }
          if (streamActive)
          {
             // beware that when the fs change crosses the boundary between
             // 2,685,312 and 2,685,313 the rx_callbacks stop for some
             // reason
             executeApiUpdate(reasonForUpdate, sdrplay_api_Update_Ext1_None,
                              waitForUpdate ? &fs_changed : nullptr, "SampleRate");
          }
       }
    }
}

double SoapySDRPlay::getSampleRate(const int direction, const size_t channel) const
{
   double fsHz = deviceParams->devParams ? deviceParams->devParams->fsFreq.fsHz : device.rspDuoSampleFreq;
   if ((fsHz == 6.0e6 && chParams->tunerParams.ifType == sdrplay_api_IF_1_620) ||
       (fsHz == 8.0e6 && chParams->tunerParams.ifType == sdrplay_api_IF_2_048))
   {
      fsHz = 2.0e6;
   }
   else if (!(fsHz >= 2.0e6 &&
              chParams->tunerParams.ifType == sdrplay_api_IF_Zero &&
              (device.hwVer != SDRPLAY_RSPduo_ID || device.rspDuoMode == sdrplay_api_RspDuoMode_Single_Tuner)
           ))
   {
      SoapySDR_logf(SOAPY_SDR_ERROR, "Invalid sample rate and/or IF setting - fsHz=%lf ifType=%d hwVer=%d rspDuoMode=%d rspDuoSampleFreq=%lf", fsHz, chParams->tunerParams.ifType, device.hwVer, device.rspDuoMode, device.rspDuoSampleFreq);
      throw std::runtime_error("Invalid sample rate and/or IF setting");
   }

   if (!chParams->ctrlParams.decimation.enable)
   {
      return fsHz;
   }
   else
   {
      unsigned int decFactor = chParams->ctrlParams.decimation.decimationFactor;
      if (decFactor == 0) decFactor = 1;  // Prevent division by zero
      return fsHz / decFactor;
   }
}

std::vector<double> SoapySDRPlay::listSampleRates(const int direction, const size_t channel) const
{
    // Use static cached vectors to avoid allocations on every call
    static const std::vector<double> RSPDUO_DUAL_RATES = {
        62500, 125000, 250000, 500000, 1000000, 2000000
    };
    static const std::vector<double> STANDARD_RATES = {
        62500, 96000, 125000, 192000, 250000, 384000, 500000, 768000,
        1000000, 2000000, 2048000, 3000000, 4000000, 5000000,
        6000000, 7000000, 8000000, 9000000, 10000000
    };

    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode != sdrplay_api_RspDuoMode_Single_Tuner)
    {
        return RSPDUO_DUAL_RATES;
    }
    return STANDARD_RATES;
}

SoapySDR::RangeList SoapySDRPlay::getSampleRateRange(const int direction, const size_t channel) const
{
    // Use static cached range lists to avoid allocations on every call
    static const SoapySDR::RangeList RSPDUO_DUAL_RANGES = {
        SoapySDR::Range(62500, 62500),
        SoapySDR::Range(125000, 125000),
        SoapySDR::Range(250000, 250000),
        SoapySDR::Range(500000, 500000),
        SoapySDR::Range(1000000, 1000000),
        SoapySDR::Range(2000000, 2000000)
    };
    static const SoapySDR::RangeList STANDARD_RANGES = {
        SoapySDR::Range(62500, 62500),
        SoapySDR::Range(96000, 96000),
        SoapySDR::Range(125000, 125000),
        SoapySDR::Range(192000, 192000),
        SoapySDR::Range(250000, 250000),
        SoapySDR::Range(384000, 384000),
        SoapySDR::Range(500000, 500000),
        SoapySDR::Range(768000, 768000),
        SoapySDR::Range(1000000, 1000000),
        SoapySDR::Range(2000000, 10660000)
    };

    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode != sdrplay_api_RspDuoMode_Single_Tuner)
    {
        return RSPDUO_DUAL_RANGES;
    }
    return STANDARD_RANGES;
}

double SoapySDRPlay::getInputSampleRateAndDecimation(uint32_t output_sample_rate, unsigned int *decM, unsigned int *decEnable, sdrplay_api_If_kHzT *ifType) const
{
    sdrplay_api_If_kHzT lif = sdrplay_api_IF_1_620;
    double lif_input_sample_rate = 6000000;
    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoSampleFreq == 8000000)
    {
        lif = sdrplay_api_IF_2_048;
        lif_input_sample_rate = 8000000;
    }

    // all RSPs should support these sample rates
    switch (output_sample_rate) {
        case 62500:
            *ifType = lif; *decM = 32; *decEnable = 1;
            return lif_input_sample_rate;
        case 125000:
            *ifType = lif; *decM = 16; *decEnable = 1;
            return lif_input_sample_rate;
        case 250000:
            *ifType = lif; *decM =  8; *decEnable = 1;
            return lif_input_sample_rate;
        case 500000:
            *ifType = lif; *decM =  4; *decEnable = 1;
            return lif_input_sample_rate;
        case 1000000:
            *ifType = lif; *decM =  2; *decEnable = 1;
            return lif_input_sample_rate;
        case 2000000:
            *ifType = lif; *decM =  1; *decEnable = 0;
            return lif_input_sample_rate;
    }

    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode != sdrplay_api_RspDuoMode_Single_Tuner)
    {
        return -1;
    }

    if (output_sample_rate <= 2000000)
    {
        switch (output_sample_rate) {
            case 96000:
                *ifType = sdrplay_api_IF_Zero; *decM = 32; *decEnable = 1;
                return output_sample_rate * *decM;
            case 192000:
                *ifType = sdrplay_api_IF_Zero; *decM = 16; *decEnable = 1;
                return output_sample_rate * *decM;
            case 384000:
                *ifType = sdrplay_api_IF_Zero; *decM =  8; *decEnable = 1;
                return output_sample_rate * *decM;
            case 768000:
                *ifType = sdrplay_api_IF_Zero; *decM =  4; *decEnable = 1;
                return output_sample_rate * *decM;
            default:
                return -1;
        }
    }

    // rate should be > 2 MHz so just return output_sample_rate
    *decM = 1; *decEnable = 0;
    *ifType = sdrplay_api_IF_Zero;
    return output_sample_rate;
}

/*******************************************************************
* Bandwidth API
******************************************************************/

void SoapySDRPlay::setBandwidth(const int direction, const size_t channel, const double bw_in)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (direction == SOAPY_SDR_RX)
   {
      // gqrx uses the value 0 for the default; in this case set it to the
      // maximum value compatible with the sample rate
      sdrplay_api_Bw_MHzT bwType = getBwEnumForRate(bw_in > 0 ? bw_in : getSampleRate(direction, channel));
      if (chParams->tunerParams.bwType != bwType)
      {
         chParams->tunerParams.bwType = bwType;
         if (streamActive)
         {
            executeApiUpdate(sdrplay_api_Update_Tuner_BwType, sdrplay_api_Update_Ext1_None,
                             nullptr, "Tuner_BwType");
         }
      }
   }
}

double SoapySDRPlay::getBandwidth(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (direction == SOAPY_SDR_RX)
   {
      return getBwValueFromEnum(chParams->tunerParams.bwType);
   }
   return 0;
}

std::vector<double> SoapySDRPlay::listBandwidths(const int direction, const size_t channel) const
{
   std::vector<double> bandwidths;
   bandwidths.push_back(200000);
   bandwidths.push_back(300000);
   bandwidths.push_back(600000);
   bandwidths.push_back(1536000);
   if (!(device.hwVer == SDRPLAY_RSPduo_ID &&
         (device.rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner ||
          device.rspDuoMode == sdrplay_api_RspDuoMode_Master ||
          device.rspDuoMode == sdrplay_api_RspDuoMode_Slave))) {
       bandwidths.push_back(5000000);
       bandwidths.push_back(6000000);
       bandwidths.push_back(7000000);
       bandwidths.push_back(8000000);
   }
   return bandwidths;
}

SoapySDR::RangeList SoapySDRPlay::getBandwidthRange(const int direction, const size_t channel) const
{
   SoapySDR::RangeList results;
   //call into the older deprecated listBandwidths() call
   for (auto &bw : this->listBandwidths(direction, channel))
   {
     results.push_back(SoapySDR::Range(bw, bw));
   }
   return results;
}

/*******************************************************************
* Bandwidth and API Update Helpers
******************************************************************/

// Helper to serialize sdrplay_api_Update calls and prevent rapid API calls from crashing
// Uses try_lock with timeout to avoid blocking indefinitely when updates come rapidly
// If changeFlag is non-null, waits for callback confirmation after update
bool SoapySDRPlay::executeApiUpdate(sdrplay_api_ReasonForUpdateT reason,
                                     sdrplay_api_ReasonForUpdateExtension1T reasonExt,
                                     std::atomic<int> *changeFlag,
                                     const char *updateName)
{
    // Try to acquire the API update mutex with a short timeout
    // If another update is in progress, skip this one to avoid queueing up
    std::unique_lock<std::timed_mutex> apiLock(api_update_mutex, std::defer_lock);
    if (!apiLock.try_lock_for(std::chrono::milliseconds(50)))
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "Skipping %s update - another update in progress", updateName);
        return false;
    }

    // Reset the change flag before update if we're waiting for confirmation
    if (changeFlag != nullptr)
    {
        *changeFlag = 0;
    }

    SdrplayApiLockGuard apiDeviceLock(SDRPLAY_API_TIMEOUT_MS);
    sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, reason, reasonExt);
    if (err != sdrplay_api_Success)
    {
        SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(%s) failed: %s", updateName, sdrplay_api_GetErrorString(err));
        return false;
    }

    // Wait for callback confirmation if a change flag was provided
    if (changeFlag != nullptr)
    {
        std::unique_lock<std::mutex> lk(update_mutex);
        if (!update_cv.wait_for(lk, std::chrono::milliseconds(updateTimeout),
                                [changeFlag]{ return *changeFlag != 0; }))
        {
            SoapySDR_logf(SOAPY_SDR_WARNING, "%s update timeout.", updateName);
        }
    }

    return true;
}

sdrplay_api_Bw_MHzT SoapySDRPlay::getBwEnumForRate(double output_sample_rate)
{
   if      (output_sample_rate <  300000) return sdrplay_api_BW_0_200;
   else if (output_sample_rate <  600000) return sdrplay_api_BW_0_300;
   else if (output_sample_rate < 1536000) return sdrplay_api_BW_0_600;
   else if (output_sample_rate < 5000000) return sdrplay_api_BW_1_536;
   else if (output_sample_rate < 6000000) return sdrplay_api_BW_5_000;
   else if (output_sample_rate < 7000000) return sdrplay_api_BW_6_000;
   else if (output_sample_rate < 8000000) return sdrplay_api_BW_7_000;
   else                                   return sdrplay_api_BW_8_000;
}

double SoapySDRPlay::getBwValueFromEnum(sdrplay_api_Bw_MHzT bwEnum)
{
   if      (bwEnum == sdrplay_api_BW_0_200) return 200000;
   else if (bwEnum == sdrplay_api_BW_0_300) return 300000;
   else if (bwEnum == sdrplay_api_BW_0_600) return 600000;
   else if (bwEnum == sdrplay_api_BW_1_536) return 1536000;
   else if (bwEnum == sdrplay_api_BW_5_000) return 5000000;
   else if (bwEnum == sdrplay_api_BW_6_000) return 6000000;
   else if (bwEnum == sdrplay_api_BW_7_000) return 7000000;
   else if (bwEnum == sdrplay_api_BW_8_000) return 8000000;
   else return 0;
}
