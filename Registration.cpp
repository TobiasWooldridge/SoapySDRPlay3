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
#include <cstdlib>

#ifdef ENABLE_SUBPROCESS_MULTIDEV
#include "SoapySDRPlayProxy.hpp"
#endif

static std::map<std::string, SoapySDR::Kwargs> _cachedResults;
static std::mutex _cachedResultsMutex;

// Clear cached device results - called on device release to force fresh enumeration
void clearCachedDeviceResults()
{
    std::lock_guard<std::mutex> cacheLock(_cachedResultsMutex);
    _cachedResults.clear();
    SoapySDR_log(SOAPY_SDR_DEBUG, "Cleared cached device results");
}

#ifdef ENABLE_SUBPROCESS_MULTIDEV
// Check if subprocess multi-device mode is enabled
// Forward declaration - moved here so findSDRPlay can use it
static bool isSubprocessModeEnabled()
{
    // Check environment variable
    const char* envVal = std::getenv("SOAPY_SDRPLAY_MULTIDEV");
    if (envVal != nullptr)
    {
        std::string val(envVal);
        return (val == "1" || val == "true" || val == "yes" || val == "on");
    }
    // Default: disabled (use standard single-device mode)
    return false;
}
#endif

static std::vector<SoapySDR::Kwargs> findSDRPlay(const SoapySDR::Kwargs &args)
{

   std::vector<SoapySDR::Kwargs> results;
   unsigned int nDevs = 0;

#ifdef ENABLE_SUBPROCESS_MULTIDEV
   // In proxy mode, check if we're looking for a specific device by serial
   bool proxyEnabled = isSubprocessModeEnabled();
   bool proxyArg = args.count("proxy") && args.at("proxy") == "true";
   SoapySDR_logf(SOAPY_SDR_DEBUG, "findSDRPlay: proxyEnabled=%d, proxyArg=%d, hasSerial=%d",
                 proxyEnabled, proxyArg, args.count("serial") > 0);

   // If proxy mode and a specific serial is requested, return synthetic entry directly
   // (skip the full enumeration for performance when serial is known)
   if ((proxyEnabled || proxyArg) && args.count("serial"))
   {
      std::string serialVal = args.at("serial");
      SoapySDR_logf(SOAPY_SDR_DEBUG, "findSDRPlay: serial from args = '%s' (len=%zu)",
                    serialVal.c_str(), serialVal.length());
      SoapySDR::Kwargs dev;
      dev["driver"] = "sdrplay";
      dev["serial"] = serialVal;
      dev["label"] = "SDRplay Proxy " + serialVal;
      dev["proxy"] = "true";
      results.push_back(dev);
      SoapySDR_logf(SOAPY_SDR_INFO, "findSDRPlay: Returning proxy device for serial %s",
                    serialVal.c_str());
      return results;
   }

   // For proxy mode without serial, prefer cached results to avoid blocking
   // API calls that hang when devices are streaming
   if (proxyEnabled || proxyArg)
   {
      std::lock_guard<std::mutex> cacheLock(_cachedResultsMutex);
      if (!_cachedResults.empty())
      {
         SoapySDR_logf(SOAPY_SDR_DEBUG, "findSDRPlay: Using cached results in proxy mode");
         for (const auto& kv : _cachedResults)
         {
            SoapySDR::Kwargs dev = kv.second;
            dev["proxy"] = "true";
            if (dev.count("label"))
            {
               dev["label"] = dev["label"] + " (proxy)";
            }
            results.push_back(dev);
         }
         return results;
      }
      // No cache - fall through to normal enumeration (first time)
   }
#endif

   // Protect access to _cachedResults throughout the function
   std::lock_guard<std::mutex> cacheLock(_cachedResultsMutex);

   // Always prefer cached results to avoid blocking streaming callbacks
   // Cache is populated on first enumeration and cleared on device release
   if (!_cachedResults.empty())
   {
      SoapySDR_logf(SOAPY_SDR_DEBUG, "findSDRPlay: Using cached results (streaming-safe)");
      for (const auto& kv : _cachedResults)
      {
         SoapySDR::Kwargs dev = kv.second;
#ifdef ENABLE_SUBPROCESS_MULTIDEV
         if (proxyEnabled || proxyArg)
         {
            dev["proxy"] = "true";
            if (dev.count("label"))
               dev["label"] = dev["label"] + " (proxy)";
         }
#endif
         results.push_back(dev);
      }
      return results;  // Early return - no blocking API calls
   }

   // Check service health before attempting enumeration
   // This prevents long timeouts when service is already known to be unresponsive
   try {
      ensureServiceResponsive();
   } catch (const std::exception &e) {
      SoapySDR_logf(SOAPY_SDR_WARNING, "Service health check failed: %s", e.what());
      // Return cached results if available
      return results;
   }

   try {
   // list devices by API
   SoapySDRPlay::sdrplay_api::get_instance();
   {
      // Use shared_ptr for device array so it survives if async times out
      auto rspDevs = std::make_shared<std::array<sdrplay_api_DeviceT, SDRPLAY_MAX_DEVICES>>();
      auto nDevsPtr = std::make_shared<unsigned int>(0);
      auto errPtr = std::make_shared<sdrplay_api_ErrT>(sdrplay_api_Success);

      // Wrap Lock + GetDevices + Unlock in async thread with timeout protection
      // CRITICAL: Lock must be acquired and released in the same thread
      auto getDevsFuture = std::make_shared<std::future<void>>(
         std::async(std::launch::async, [rspDevs, nDevsPtr, errPtr]() {
            sdrplay_api_ErrT lockErr = sdrplay_api_LockDeviceApi();
            if (lockErr != sdrplay_api_Success) {
               *errPtr = lockErr;
               return;
            }
            *errPtr = sdrplay_api_GetDevices(rspDevs->data(), nDevsPtr.get(), SDRPLAY_MAX_DEVICES);
            sdrplay_api_UnlockDeviceApi();
         })
      );

      auto status = getDevsFuture->wait_for(std::chrono::milliseconds(SDRPLAY_API_TIMEOUT_MS));
      if (status == std::future_status::timeout) {
         SoapySDR_log(SOAPY_SDR_ERROR, "sdrplay_api_GetDevices() timed out - service is unresponsive");
         recordApiTimeout();  // Track timeout for health monitoring

         // Detach to prevent destructor blocking; shared_ptrs keep data alive
         std::thread([getDevsFuture, rspDevs, nDevsPtr, errPtr]() {
            try { getDevsFuture->get(); } catch (...) {}
         }).detach();

         // Attempt to force-restart service after timeout (SIGHUP won't help if hung)
         SoapySDR_log(SOAPY_SDR_WARNING, "Attempting to force-restart SDRplay service after timeout...");
         int restartResult = std::system("sudo -n sdrplay-service-restart --force 2>/dev/null");
         if (restartResult == 0) {
            resetServiceHealthTracking();
            SoapySDR_log(SOAPY_SDR_INFO, "Service restart requested - retry device enumeration");
         }

         throw std::runtime_error("sdrplay_api_GetDevices() timed out");
      }

      getDevsFuture->get();  // Check for exceptions
      sdrplay_api_ErrT err = *errPtr;
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

#ifdef ENABLE_SUBPROCESS_MULTIDEV
   // If proxy mode is enabled (but no serial was specified), convert all results to proxy entries
   // This allows enumeration to work while still using proxy mode for device access
   if ((proxyEnabled || proxyArg) && !results.empty())
   {
      for (auto& dev : results)
      {
         dev["proxy"] = "true";
         // Update label to indicate proxy mode
         if (dev.count("label"))
         {
            dev["label"] = dev["label"] + " (proxy)";
         }
      }
      SoapySDR_logf(SOAPY_SDR_INFO, "findSDRPlay: Converted %zu devices to proxy mode",
                    results.size());
   }
#endif

   return results;
}

static SoapySDR::Device *makeSDRPlay(const SoapySDR::Kwargs &args)
{
#ifdef ENABLE_SUBPROCESS_MULTIDEV
    // Debug: log received args
    std::string argsStr;
    for (const auto& kv : args) {
        if (!argsStr.empty()) argsStr += ", ";
        argsStr += kv.first + "=" + kv.second;
    }
    SoapySDR_logf(SOAPY_SDR_DEBUG, "makeSDRPlay: args={%s}", argsStr.c_str());

    // Use proxy mode if:
    // 1. Environment variable SOAPY_SDRPLAY_MULTIDEV is set, OR
    // 2. Device args contain "proxy=true"
    bool useProxy = isSubprocessModeEnabled();

    if (args.count("proxy") && args.at("proxy") == "true")
    {
        useProxy = true;
    }

    if (useProxy)
    {
        SoapySDR_log(SOAPY_SDR_INFO, "Using subprocess proxy mode for SDRplay device");
        return new SoapySDRPlayProxy(args);
    }
#endif

    return new SoapySDRPlay(args);
}

static SoapySDR::Registry registerSDRPlay("sdrplay", &findSDRPlay, &makeSDRPlay, SOAPY_SDR_ABI_VERSION);
