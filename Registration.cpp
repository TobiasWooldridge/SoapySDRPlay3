/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2020 Franco Venturi - changes for SDRplay API version 3
 *                                     and Dual Tuner for RSPduo

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapySDRPlay.hpp"
#include <SoapySDR/Registry.hpp>
#include <mutex>

static std::map<std::string, SoapySDR::Kwargs> _cachedResults;
static std::mutex _cachedResultsMutex;

static std::vector<SoapySDR::Kwargs> findSDRPlay(const SoapySDR::Kwargs &args)
{
   std::vector<SoapySDR::Kwargs> results;
   unsigned int nDevs = 0;


   // Protect access to _cachedResults throughout the function
   std::lock_guard<std::mutex> cacheLock(_cachedResultsMutex);


   try {
   // list devices by API
   SoapySDRPlay::sdrplay_api::get_instance();
   {
      SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);

      // Use shared_ptr for device array so it survives if async times out
      auto rspDevs = std::make_shared<std::array<sdrplay_api_DeviceT, SDRPLAY_MAX_DEVICES>>();
      auto nDevsPtr = std::make_shared<unsigned int>(0);

      // Wrap GetDevices with timeout protection
      auto getDevsFuture = std::make_shared<std::future<sdrplay_api_ErrT>>(
         std::async(std::launch::async, [rspDevs, nDevsPtr]() {
            return sdrplay_api_GetDevices(rspDevs->data(), nDevsPtr.get(), SDRPLAY_MAX_DEVICES);
         })
      );

      auto status = getDevsFuture->wait_for(std::chrono::milliseconds(SDRPLAY_API_TIMEOUT_MS));
      if (status == std::future_status::timeout) {
         SoapySDR_log(SOAPY_SDR_ERROR, "sdrplay_api_GetDevices() timed out - service is unresponsive");
         // Detach to prevent destructor blocking; shared_ptrs keep data alive
         std::thread([getDevsFuture, rspDevs, nDevsPtr]() {
            try { getDevsFuture->get(); } catch (...) {}
         }).detach();
         throw std::runtime_error("sdrplay_api_GetDevices() timed out");
      }

      sdrplay_api_ErrT err = getDevsFuture->get();
      nDevs = *nDevsPtr;
      if (err != sdrplay_api_Success) {
         SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_GetDevices() failed: %s", sdrplay_api_GetErrorString(err));
         throw std::runtime_error("sdrplay_api_GetDevices() failed");
      }

      for (unsigned int i = 0; i < nDevs; i++)
      {
      if (!(*rspDevs)[i].valid) continue;
      SoapySDR::Kwargs dev;
      dev["serial"] = (*rspDevs)[i].SerNo;
      const bool serialMatch = args.count("serial") == 0 || args.at("serial") == dev["serial"];
      if (!serialMatch) continue;
      std::string modelName;
      if ((*rspDevs)[i].hwVer == SDRPLAY_RSP1_ID)
      {
         modelName = "RSP1";
      }
      else if ((*rspDevs)[i].hwVer == SDRPLAY_RSP1A_ID)
      {
         modelName = "RSP1A";
      }
      else if ((*rspDevs)[i].hwVer == SDRPLAY_RSP1B_ID)
      {
         modelName = "RSP1B";
      }
      else if ((*rspDevs)[i].hwVer == SDRPLAY_RSP2_ID)
      {
         modelName = "RSP2";
      }
      else if ((*rspDevs)[i].hwVer == SDRPLAY_RSPdx_ID)
      {
         modelName = "RSPdx";
      }
      else if ((*rspDevs)[i].hwVer == SDRPLAY_RSPdxR2_ID)
      {
         modelName = "RSPdx-R2";
      }
      else
      {
         modelName = "UNKNOWN";
      }
      if ((*rspDevs)[i].hwVer != SDRPLAY_RSPduo_ID)
      {
         dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo;
         results.push_back(dev);
         _cachedResults[dev["serial"]] = dev;
         continue;
      }

      // RSPduo case
      modelName = "RSPduo";
      if ((*rspDevs)[i].rspDuoMode & sdrplay_api_RspDuoMode_Single_Tuner)
      {
         dev["mode"] = "ST";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo + " - Single Tuner";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if ((*rspDevs)[i].rspDuoMode & sdrplay_api_RspDuoMode_Dual_Tuner)
      {
         dev["mode"] = "DT";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo + " - Dual Tuner";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if ((*rspDevs)[i].rspDuoMode & sdrplay_api_RspDuoMode_Master)
      {
         dev["mode"] = "MA";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo + " - Master";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if ((*rspDevs)[i].rspDuoMode & sdrplay_api_RspDuoMode_Master)
      {
         dev["mode"] = "MA8";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo + " - Master (RSPduo sample rate=8Mhz)";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if ((*rspDevs)[i].rspDuoMode & sdrplay_api_RspDuoMode_Slave)
      {
         dev["mode"] = "SL";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + (*rspDevs)[i].SerNo + " - Slave";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      }
   } // RAII guard releases API lock here

   // fill in the cached results for claimed handles
   for (const auto &serial : SoapySDRPlay_getClaimedSerials())
   {
      if (_cachedResults.count(serial) == 0) continue;
      if (args.count("serial") != 0)
      {
         std::string cacheKey = args.at("serial");
         if (args.count("mode") != 0) cacheKey += "@" + args.at("mode");
         if (cacheKey != serial) continue;
      }
      results.push_back(_cachedResults.at(serial));
   }

   } catch (const std::exception &e) {
      SoapySDR_logf(SOAPY_SDR_ERROR, "SDRplay enumeration failed: %s", e.what());
      // Return cached results on error (may be empty)
   }

   return results;
}

static SoapySDR::Device *makeSDRPlay(const SoapySDR::Kwargs &args)
{
    return new SoapySDRPlay(args);
}

static SoapySDR::Registry registerSDRPlay("sdrplay", &findSDRPlay, &makeSDRPlay, SOAPY_SDR_ABI_VERSION);
