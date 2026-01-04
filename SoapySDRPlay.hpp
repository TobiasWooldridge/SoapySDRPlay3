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
#pragma once

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Types.h>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <string>
#include <cstring>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <future>
#include <chrono>

#include <sdrplay_api.h>
#include <functional>

// Default timeout for SDRplay API operations (in milliseconds)
// This prevents indefinite hangs when the SDRplay service is unresponsive
#define SDRPLAY_API_TIMEOUT_MS 10000

// Service health tracking functions (defined in sdrplay_api.cpp)
void recordApiTimeout();
void recordApiSuccess();
bool isServiceResponsive();
int getConsecutiveTimeouts();
void resetServiceHealthTracking();
void ensureServiceResponsive();  // Throws if service unresponsive and can't be restarted

// RAII guard for SDRplay API lock to prevent lock leaks on exceptions
// Supports optional timeout to prevent indefinite hangs
class SdrplayApiLockGuard {
public:
    // Constructor with optional timeout (ignored - kept for API compatibility)
    // NOTE: Timeout was removed because the lock must be acquired in the same
    // thread that makes subsequent API calls. Using async for lock acquisition
    // breaks thread-safety assumptions in the SDRplay API.
    explicit SdrplayApiLockGuard(unsigned int /* timeoutMs */ = 0)
        : acquired(false)
    {
        if (lockDepth++ == 0)
        {
            // Directly acquire lock - no timeout to avoid thread-safety issues
            sdrplay_api_LockDeviceApi();
            acquired = true;
        }
    }
    ~SdrplayApiLockGuard()
    {
        if (lockDepth == 0)
        {
            return;
        }
        if (--lockDepth == 0 && acquired)
        {
            // Directly release lock
            sdrplay_api_UnlockDeviceApi();
        }
    }
    SdrplayApiLockGuard(const SdrplayApiLockGuard&) = delete;
    SdrplayApiLockGuard& operator=(const SdrplayApiLockGuard&) = delete;

private:
    static thread_local unsigned int lockDepth;
    bool acquired;
};

#define DEFAULT_BUFFER_LENGTH     (65536)
#define DEFAULT_NUM_BUFFERS       (8)
#define DEFAULT_ELEMS_PER_SAMPLE  (2)

/*******************************************************************
 * Health Monitoring and Recovery Types
 ******************************************************************/

// Device health status for monitoring and recovery
enum class DeviceHealthStatus {
    Healthy,              // Stream active, callbacks arriving normally
    Warning,              // Minor issues (slow callbacks, high timeouts)
    Stale,                // Callbacks stopped arriving
    Recovering,           // Recovery in progress
    ServiceUnresponsive,  // API calls timing out
    DeviceRemoved,        // USB device disconnected
    Failed                // Unrecoverable failure
};

// Detailed health information
struct HealthInfo {
    DeviceHealthStatus status = DeviceHealthStatus::Healthy;
    uint64_t callbackCount = 0;
    double callbackRate = 0.0;           // callbacks per second
    int consecutiveTimeouts = 0;
    int recoveryAttempts = 0;
    int successfulRecoveries = 0;
    std::string lastError;
    std::chrono::steady_clock::time_point lastHealthyTime;
};

// Cached device settings for recovery
struct DeviceSettingsCache {
    // Frequency settings
    double rfFrequencyHz = 200000000;
    double ppmCorrection = 0.0;

    // Gain settings
    int lnaState = 4;
    int ifGainReduction = 40;
    bool agcEnabled = false;
    int agcSetPoint = -30;

    // Sample rate and bandwidth
    double sampleRate = 2000000;
    double bandwidth = 0;  // 0 = auto

    // Decimation
    unsigned int decimationFactor = 1;
    bool decimationEnabled = false;

    // Device-specific settings
    bool biasTEnabled = false;
    bool rfNotchEnabled = false;
    bool dabNotchEnabled = false;
    bool extRefEnabled = false;
    bool hdrEnabled = false;

    // Antenna
    std::string antennaName;

    // DC/IQ correction
    bool dcCorrectionEnabled = true;
    bool iqCorrectionEnabled = true;

    // Validity
    std::chrono::steady_clock::time_point savedAt;
    bool isValid = false;
};

// Watchdog configuration
struct WatchdogConfig {
    bool enabled = true;
    int callbackTimeoutMs = 2000;      // Max time between callbacks before stale
    int healthCheckIntervalMs = 500;   // How often to check health
    int maxRecoveryAttempts = 3;       // Per session
    int recoveryBackoffMs = 1000;      // Initial backoff between attempts
    bool autoRecover = true;           // Automatic vs manual recovery
    bool restartServiceOnFailure = true;   // Try to restart SDRplay service
    bool usbResetOnFailure = false;        // Try USB power cycle (requires uhubctl)
};

// Recovery result codes
enum class RecoveryResult {
    Success,
    FailedUninit,
    FailedInit,
    FailedSettings,
    MaxAttemptsExceeded,
    ServiceDown,
    InProgress
};

// Ensure numBuffers is a power of 2 for efficient ring buffer operations
static_assert((DEFAULT_NUM_BUFFERS & (DEFAULT_NUM_BUFFERS - 1)) == 0,
              "DEFAULT_NUM_BUFFERS must be a power of 2");

// Thread-safe claimed serials access
std::set<std::string> SoapySDRPlay_getClaimedSerials(void);  // Returns copy for safe iteration
void SoapySDRPlay_claimSerial(const std::string &serial);
void SoapySDRPlay_releaseSerial(const std::string &serial);

class SoapySDRPlay: public SoapySDR::Device
{
public:
    explicit SoapySDRPlay(const SoapySDR::Kwargs &args);

    ~SoapySDRPlay(void);

    /*******************************************************************
     * Identification API
     ******************************************************************/

    std::string getDriverKey(void) const;

    std::string getHardwareKey(void) const;

    SoapySDR::Kwargs getHardwareInfo(void) const;

    /*******************************************************************
     * Channels API
     ******************************************************************/

    size_t getNumChannels(const int) const;

    /*******************************************************************
     * Stream API
     ******************************************************************/

    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const;

    std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const;

    SoapySDR::ArgInfoList getStreamArgsInfo(const int direction, const size_t channel) const;

    SoapySDR::Stream *setupStream(const int direction, 
                                  const std::string &format, 
                                  const std::vector<size_t> &channels = std::vector<size_t>(), 
                                  const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    void closeStream(SoapySDR::Stream *stream);

    size_t getStreamMTU(SoapySDR::Stream *stream) const;

    int activateStream(SoapySDR::Stream *stream,
                       const int flags = 0,
                       const long long timeNs = 0,
                       const size_t numElems = 0);

    int deactivateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0);

    int readStream(SoapySDR::Stream *stream,
                   void * const *buffs,
                   const size_t numElems,
                   int &flags,
                   long long &timeNs,
                   const long timeoutUs = 200000);

    /*******************************************************************
     * Direct buffer access API
     ******************************************************************/

    size_t getNumDirectAccessBuffers(SoapySDR::Stream *stream);

    int getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **buffs);

    int acquireReadBuffer(SoapySDR::Stream *stream,
                          size_t &handle,
                          const void **buffs,
                          int &flags,
                          long long &timeNs,
                          const long timeoutUs = 100000);

    void releaseReadBuffer(SoapySDR::Stream *stream, const size_t handle);

    /*******************************************************************
     * Antenna API
     ******************************************************************/

    std::vector<std::string> listAntennas(const int direction, const size_t channel) const;

    void setAntenna(const int direction, const size_t channel, const std::string &name);

    std::string getAntenna(const int direction, const size_t channel) const;

    void setAntennaPersistent(const int direction, const size_t channel,
                              const std::string &name, const bool persistent = true);

    bool getAntennaPersistent(const int direction, const size_t channel) const;

    /*******************************************************************
     * Frontend corrections API
     ******************************************************************/

    bool hasDCOffsetMode(const int direction, const size_t channel) const;

    bool hasFrequencyCorrection(const int direction, const size_t channel) const;

    void setFrequencyCorrection(const int direction, const size_t channel, const double value);

    double getFrequencyCorrection(const int direction, const size_t channel) const;

    /*******************************************************************
     * Gain API
     ******************************************************************/

    std::vector<std::string> listGains(const int direction, const size_t channel) const;

    bool hasGainMode(const int direction, const size_t channel) const;

    void setGainMode(const int direction, const size_t channel, const bool automatic);

    bool getGainMode(const int direction, const size_t channel) const;

    void setGain(const int direction, const size_t channel, const std::string &name, const double value);

    double getGain(const int direction, const size_t channel, const std::string &name) const;

    SoapySDR::Range getGainRange(const int direction, const size_t channel, const std::string &name) const;

    // Overall gain methods (distributes gain across LNA and IF stages)
    void setGain(const int direction, const size_t channel, const double value);

    double getGain(const int direction, const size_t channel) const;

    SoapySDR::Range getGainRange(const int direction, const size_t channel) const;

    /*******************************************************************
     * Frequency API
     ******************************************************************/

    void setFrequency(const int direction,
                      const size_t channel,
                      const double frequency,
                      const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    void setFrequency(const int direction,
                      const size_t channel,
                      const std::string &name,
                      const double frequency,
                      const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    double getFrequency(const int direction, const size_t channel) const;

    double getFrequency(const int direction, const size_t channel, const std::string &name) const;

    SoapySDR::RangeList getBandwidthRange(const int direction, const size_t channel) const;

    std::vector<std::string> listFrequencies(const int direction, const size_t channel) const;

    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel) const;

    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel, const std::string &name) const;

    SoapySDR::ArgInfoList getFrequencyArgsInfo(const int direction, const size_t channel) const;

    /*******************************************************************
     * Sample Rate API
     ******************************************************************/

    void setSampleRate(const int direction, const size_t channel, const double rate);

    double getSampleRate(const int direction, const size_t channel) const;

    std::vector<double> listSampleRates(const int direction, const size_t channel) const;

    SoapySDR::RangeList getSampleRateRange(const int direction, const size_t channel) const;

    /*******************************************************************
    * Bandwidth API
    ******************************************************************/

    void setBandwidth(const int direction, const size_t channel, const double bw);

    double getBandwidth(const int direction, const size_t channel) const;

    std::vector<double> listBandwidths(const int direction, const size_t channel) const;
    
    void setDCOffsetMode(const int direction, const size_t channel, const bool automatic);
    
    bool getDCOffsetMode(const int direction, const size_t channel) const;
    
    bool hasDCOffset(const int direction, const size_t channel) const;

    /*******************************************************************
     * Settings API
     ******************************************************************/

    SoapySDR::ArgInfoList getSettingInfo(void) const;

    void writeSetting(const std::string &key, const std::string &value);

    std::string readSetting(const std::string &key) const;

    /*******************************************************************
     * Health Monitoring API
     ******************************************************************/

    DeviceHealthStatus getHealthStatus() const;

    HealthInfo getHealthInfo() const;

    void registerHealthCallback(std::function<void(DeviceHealthStatus)> callback);

    // Manual recovery control
    bool triggerRecovery();
    bool restartService();
    bool resetUSBDevice();

    // Watchdog configuration
    WatchdogConfig getWatchdogConfig() const;
    void setWatchdogConfig(const WatchdogConfig& config);

    /*******************************************************************
     * Async API
     ******************************************************************/

    class SoapySDRPlayStream;
    void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, SoapySDRPlayStream *stream);

    void ev_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params);

    /*******************************************************************
     * public utility static methods
     ******************************************************************/

    static unsigned char stringToHWVer(std::string hwVer);

    static std::string HWVertoString(unsigned char hwVer);

    static sdrplay_api_RspDuoModeT stringToRSPDuoMode(std::string rspDuoMode);

    static std::string RSPDuoModetoString(sdrplay_api_RspDuoModeT rspDuoMode);

#ifdef SOAPYSDRPLAY_ENABLE_TESTS
    static sdrplay_api_Bw_MHzT test_getBwEnumForRate(double output_sample_rate)
    {
        return getBwEnumForRate(output_sample_rate);
    }

    static double test_getBwValueFromEnum(sdrplay_api_Bw_MHzT bwEnum)
    {
        return getBwValueFromEnum(bwEnum);
    }
#endif

private:

    // Friend class for exception-safe device cleanup in constructor
    friend class DeviceSelectionGuard;

    /*******************************************************************
     * Internal functions
     ******************************************************************/

    double getInputSampleRateAndDecimation(uint32_t output_sample_rate, unsigned int *decM, unsigned int *decEnable, sdrplay_api_If_kHzT *ifType) const;

    // Helper to serialize sdrplay_api_Update calls and optionally wait for callback
    // Returns true on success, false if update was skipped due to contention
    bool executeApiUpdate(sdrplay_api_ReasonForUpdateT reason,
                          sdrplay_api_ReasonForUpdateExtension1T reasonExt,
                          std::atomic<int> *changeFlag,
                          const char *updateName);

    static std::string makeAntennaPersistKey(const std::string &serial, const std::string &mode);

    std::string loadPersistedAntenna(const std::string &key, const size_t channel) const;

    void savePersistedAntenna(const std::string &key, const size_t channel, const std::string &name) const;

    static sdrplay_api_Bw_MHzT getBwEnumForRate(double output_sample_rate);

    static double getBwValueFromEnum(sdrplay_api_Bw_MHzT bwEnum);

    void selectDevice(const std::string &serial, const std::string &mode, const std::string &antenna);

    void selectDevice();

    void selectDevice(sdrplay_api_TunerSelectT tuner,
                      sdrplay_api_RspDuoModeT rspDuoMode,
                      double rspDuoSampleFreq,
                      sdrplay_api_DeviceParamsT *thisDeviceParams);

    void releaseDevice();

#ifdef SHOW_SERIAL_NUMBER_IN_MESSAGES
    void SoapySDR_log(const SoapySDRLogLevel logLevel, const char *message) const;
    void SoapySDR_logf(const SoapySDRLogLevel logLevel, const char *format, ...) const;
#endif


    /*******************************************************************
     * Private variables
     ******************************************************************/
    //device settings
    sdrplay_api_DeviceT device;
    sdrplay_api_DeviceParamsT *deviceParams;
    sdrplay_api_RxChannelParamsT *chParams;
    int hwVer;
    std::string serNo;
    std::string cacheKey;
    // RSP device id is used to identify the device in 'selectedRSPDevices'
    //  - serial number for RSP (except the RSPduo) and the RSPduo in non-slave mode
    //  - serial number/S for the RSPduo in slave mode
    std::string rspDeviceId;

    //cached settings
    std::atomic_ulong bufferLength;
    std::atomic_ulong cachedBufferThreshold;  // bufferLength / decFactor, cached for hot path

    //numBuffers, bufferElems, elementsPerSample
    //are indeed constants
    const size_t numBuffers = DEFAULT_NUM_BUFFERS;
    const unsigned int bufferElems = DEFAULT_BUFFER_LENGTH;
    const int elementsPerSample = DEFAULT_ELEMS_PER_SAMPLE;

    std::atomic_bool streamActive;

    std::atomic_bool useShort;

    const int uninitRetryDelay = 10;   // 10 seconds before trying uninit again 

    static std::unordered_map<std::string, sdrplay_api_DeviceT*> selectedRSPDevices;

    // RX callback reporting changes to gain reduction, frequency, sample rate
    std::atomic<int> gr_changed;
    std::atomic<int> rf_changed;
    std::atomic<int> fs_changed;

    // Track desired gain values to coordinate LNA and IF gain settings
    // This prevents conflicts when element gains are set independently
    int desired_lna_state = 4;   // Default LNAstate (mid-range for RSPdx)
    int desired_if_gr = 40;      // Default IFGR (mid-range)
    // event callback reporting device is unavailable
    std::atomic<bool> device_unavailable;
    const int updateTimeout = 500;   // 500ms timeout for updates

    // Condition variable for parameter update notifications from callback
    std::mutex update_mutex;
    std::condition_variable update_cv;

    // Antenna persistence (SoapySDR fork API)
    // When enabled, antenna setting is re-applied after activateStream()
    bool antennaPersistentEnabled[2] = {false, false};
    std::string persistentAntennaName[2];

    // Mutex to serialize sdrplay_api_Update() calls
    // This prevents rapid successive API calls from overwhelming the hardware
    // Uses timed_mutex to allow try_lock_for() with timeout
    std::timed_mutex api_update_mutex;

    /*******************************************************************
     * Health Monitoring and Recovery (private)
     ******************************************************************/

    // Health monitoring
    HealthInfo healthInfo;
    mutable std::mutex healthInfoMutex;
    std::vector<std::function<void(DeviceHealthStatus)>> healthCallbacks;
    std::mutex healthCallbacksMutex;

    void updateHealthStatus(DeviceHealthStatus newStatus);
    void notifyHealthCallbacks(DeviceHealthStatus status);

    // Settings cache for recovery
    DeviceSettingsCache settingsCache;
    mutable std::mutex settingsCacheMutex;

    void saveCurrentSettings();
    bool restoreSettings();
    void invalidateSettingsCache();

    // Watchdog thread
    WatchdogConfig watchdogConfig;
    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{false};
    std::atomic<bool> watchdogShutdown{false};

    void watchdogThreadFunc();
    void startWatchdog();
    void stopWatchdog();
    void checkServiceHealth();

    // Recovery state
    std::atomic<bool> recoveryInProgress{false};
    std::atomic<int> recoveryAttemptCount{0};
    std::chrono::steady_clock::time_point lastRecoveryAttempt;

    RecoveryResult attemptStreamRecovery();
    void handleStaleStream();

public:

    /*******************************************************************
     * Public variables
     ******************************************************************/
    
    mutable std::mutex _general_state_mutex;
    mutable std::mutex _streams_mutex;

    class SoapySDRPlayStream
    {
    public:
        SoapySDRPlayStream(size_t channel, size_t numBuffers, unsigned long bufferLength);
        ~SoapySDRPlayStream(void);

        size_t channel;

        std::mutex mutex;
        std::condition_variable cond;

        std::vector<std::vector<short> > shortBuffs;
        std::vector<std::vector<float> > floatBuffs;
        size_t      head;
        size_t      tail;
        /// number of in-flight buffers
        size_t      count;
        void *currentBuff;
        bool overflowEvent;
        std::atomic_size_t nElems;
        size_t currentHandle;
        std::atomic_bool reset;

        // Mutex for serializing readStream() operations
        // (separate from buffer mutex to avoid deadlock)
        std::mutex readStreamMutex;

        // Callback activity tracking - detects if callbacks stop firing
        std::atomic<uint64_t> lastCallbackTicks{0};

        // Sample gap detection - tracks expected next sample number
        unsigned int nextSampleNum{0};
        std::atomic<uint64_t> sampleGapCount{0};  // Total gaps detected

        // Watchdog tracking
        uint64_t lastWatchdogTicks{0};
        std::chrono::steady_clock::time_point lastCallbackTime;
    };

    SoapySDRPlayStream *_streams[2];
    int _streamsRefCount[2];

    constexpr static double defaultRspDuoSampleFreq = 6000000;
    constexpr static double defaultRspDuoOutputSampleRate = 2000000;

    // Singleton class for SDRplay API (only one per process)
    class sdrplay_api
    {
    public:
        static sdrplay_api& get_instance()
        {
            static sdrplay_api instance;
            return instance;
        }
        static float get_version()
        {
            return ver;
        }

    private:
        static float ver;
        sdrplay_api();

    public:
        ~sdrplay_api();
        sdrplay_api(const sdrplay_api&) = delete;
        sdrplay_api& operator=(const sdrplay_api&) = delete;
    };
};
