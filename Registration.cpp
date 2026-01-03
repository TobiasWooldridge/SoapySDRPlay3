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

   fprintf(stderr, "[SDRplay] findSDRPlay: starting\n"); fflush(stderr);

   // Protect access to _cachedResults throughout the function
   std::lock_guard<std::mutex> cacheLock(_cachedResultsMutex);

   fprintf(stderr, "[SDRplay] findSDRPlay: acquired cache lock\n"); fflush(stderr);

   try {
   // list devices by API
   fprintf(stderr, "[SDRplay] findSDRPlay: calling get_instance()\n"); fflush(stderr);
   SoapySDRPlay::sdrplay_api::get_instance();
   fprintf(stderr, "[SDRplay] findSDRPlay: got instance\n"); fflush(stderr);
   {
      SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
      fprintf(stderr, "[SDRplay] findSDRPlay: acquired API lock, calling GetDevices\n"); fflush(stderr);
      sdrplay_api_DeviceT rspDevs[SDRPLAY_MAX_DEVICES];
      sdrplay_api_GetDevices(&rspDevs[0], &nDevs, SDRPLAY_MAX_DEVICES);

      for (unsigned int i = 0; i < nDevs; i++)
      {
      if (!rspDevs[i].valid) continue;
      SoapySDR::Kwargs dev;
      dev["serial"] = rspDevs[i].SerNo;
      const bool serialMatch = args.count("serial") == 0 || args.at("serial") == dev["serial"];
      if (!serialMatch) continue;
      std::string modelName;
      if (rspDevs[i].hwVer == SDRPLAY_RSP1_ID)
      {
         modelName = "RSP1";
      }
      else if (rspDevs[i].hwVer == SDRPLAY_RSP1A_ID)
      {
         modelName = "RSP1A";
      }
      else if (rspDevs[i].hwVer == SDRPLAY_RSP1B_ID)
      {
         modelName = "RSP1B";
      }
      else if (rspDevs[i].hwVer == SDRPLAY_RSP2_ID)
      {
         modelName = "RSP2";
      }
      else if (rspDevs[i].hwVer == SDRPLAY_RSPdx_ID)
      {
         modelName = "RSPdx";
      }
      else if (rspDevs[i].hwVer == SDRPLAY_RSPdxR2_ID)
      {
         modelName = "RSPdx-R2";
      }
      else
      {
         modelName = "UNKNOWN";
      }
      if (rspDevs[i].hwVer != SDRPLAY_RSPduo_ID)
      {
         dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo;
         results.push_back(dev);
         _cachedResults[dev["serial"]] = dev;
         continue;
      }

      // RSPduo case
      modelName = "RSPduo";
      if (rspDevs[i].rspDuoMode & sdrplay_api_RspDuoMode_Single_Tuner)
      {
         dev["mode"] = "ST";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo + " - Single Tuner";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if (rspDevs[i].rspDuoMode & sdrplay_api_RspDuoMode_Dual_Tuner)
      {
         dev["mode"] = "DT";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo + " - Dual Tuner";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if (rspDevs[i].rspDuoMode & sdrplay_api_RspDuoMode_Master)
      {
         dev["mode"] = "MA";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo + " - Master";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if (rspDevs[i].rspDuoMode & sdrplay_api_RspDuoMode_Master)
      {
         dev["mode"] = "MA8";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo + " - Master (RSPduo sample rate=8Mhz)";
            results.push_back(dev);
            _cachedResults[dev["serial"] + "@" + dev["mode"]] = dev;
         }
      }
      if (rspDevs[i].rspDuoMode & sdrplay_api_RspDuoMode_Slave)
      {
         dev["mode"] = "SL";
         const bool modeMatch = args.count("mode") == 0 || args.at("mode") == dev["mode"];
         if (modeMatch)
         {
            dev["label"] = "SDRplay Dev" + std::to_string(results.size()) + " " + modelName + " " + rspDevs[i].SerNo + " - Slave";
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
      fprintf(stderr, "[SDRplay] findSDRPlay: exception: %s\n", e.what()); fflush(stderr);
      SoapySDR_logf(SOAPY_SDR_ERROR, "SDRplay enumeration failed: %s", e.what());
      // Return cached results on error (may be empty)
   }

   fprintf(stderr, "[SDRplay] findSDRPlay: returning\n"); fflush(stderr);
   return results;
}

static SoapySDR::Device *makeSDRPlay(const SoapySDR::Kwargs &args)
{
    return new SoapySDRPlay(args);
}

static SoapySDR::Registry registerSDRPlay("sdrplay", &findSDRPlay, &makeSDRPlay, SOAPY_SDR_ABI_VERSION);
