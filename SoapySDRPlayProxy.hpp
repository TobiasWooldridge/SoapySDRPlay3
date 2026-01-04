/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2020 Franco Venturi - changes for SDRplay API version 3
 * Copyright (c) 2025 - subprocess multi-device support

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
#include "IPCPipe.hpp"
#include "RingBuffer.hpp"
#include "SoapySDRPlayWorker.hpp"

#include <memory>
#include <string>
#include <atomic>

// Proxy device that forwards to a worker subprocess
// Implements SoapySDR::Device interface transparently
class SoapySDRPlayProxy : public SoapySDR::Device
{
public:
    explicit SoapySDRPlayProxy(const SoapySDR::Kwargs& args);
    ~SoapySDRPlayProxy() override;

    // Identification API
    std::string getDriverKey() const override;
    std::string getHardwareKey() const override;
    SoapySDR::Kwargs getHardwareInfo() const override;

    // Channels API
    size_t getNumChannels(const int direction) const override;
    SoapySDR::Kwargs getChannelInfo(const int direction, const size_t channel) const override;

    // Stream API
    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const override;
    std::string getNativeStreamFormat(const int direction, const size_t channel, double& fullScale) const override;
    SoapySDR::ArgInfoList getStreamArgsInfo(const int direction, const size_t channel) const override;

    SoapySDR::Stream* setupStream(
        const int direction,
        const std::string& format,
        const std::vector<size_t>& channels = std::vector<size_t>(),
        const SoapySDR::Kwargs& args = SoapySDR::Kwargs()) override;

    void closeStream(SoapySDR::Stream* stream) override;
    size_t getStreamMTU(SoapySDR::Stream* stream) const override;
    int activateStream(SoapySDR::Stream* stream, const int flags = 0, const long long timeNs = 0, const size_t numElems = 0) override;
    int deactivateStream(SoapySDR::Stream* stream, const int flags = 0, const long long timeNs = 0) override;

    int readStream(
        SoapySDR::Stream* stream,
        void* const* buffs,
        const size_t numElems,
        int& flags,
        long long& timeNs,
        const long timeoutUs = 100000) override;

    // Direct buffer access API
    size_t getNumDirectAccessBuffers(SoapySDR::Stream* stream) override;
    int getDirectAccessBufferAddrs(SoapySDR::Stream* stream, const size_t handle, void** buffs) override;
    int acquireReadBuffer(SoapySDR::Stream* stream, size_t& handle, const void** buffs, int& flags, long long& timeNs, const long timeoutUs = 100000) override;
    void releaseReadBuffer(SoapySDR::Stream* stream, const size_t handle) override;

    // Antenna API
    std::vector<std::string> listAntennas(const int direction, const size_t channel) const override;
    void setAntenna(const int direction, const size_t channel, const std::string& name) override;
    std::string getAntenna(const int direction, const size_t channel) const override;

    // Frontend corrections API
    bool hasDCOffsetMode(const int direction, const size_t channel) const override;
    void setDCOffsetMode(const int direction, const size_t channel, const bool automatic) override;
    bool getDCOffsetMode(const int direction, const size_t channel) const override;

    // Gain API
    std::vector<std::string> listGains(const int direction, const size_t channel) const override;
    bool hasGainMode(const int direction, const size_t channel) const override;
    void setGainMode(const int direction, const size_t channel, const bool automatic) override;
    bool getGainMode(const int direction, const size_t channel) const override;
    void setGain(const int direction, const size_t channel, const double value) override;
    void setGain(const int direction, const size_t channel, const std::string& name, const double value) override;
    double getGain(const int direction, const size_t channel) const override;
    double getGain(const int direction, const size_t channel, const std::string& name) const override;
    SoapySDR::Range getGainRange(const int direction, const size_t channel) const override;
    SoapySDR::Range getGainRange(const int direction, const size_t channel, const std::string& name) const override;

    // Frequency API
    void setFrequency(const int direction, const size_t channel, const double frequency, const SoapySDR::Kwargs& args = SoapySDR::Kwargs()) override;
    void setFrequency(const int direction, const size_t channel, const std::string& name, const double frequency, const SoapySDR::Kwargs& args = SoapySDR::Kwargs()) override;
    double getFrequency(const int direction, const size_t channel) const override;
    double getFrequency(const int direction, const size_t channel, const std::string& name) const override;
    std::vector<std::string> listFrequencies(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel, const std::string& name) const override;
    SoapySDR::ArgInfoList getFrequencyArgsInfo(const int direction, const size_t channel) const override;

    // Sample rate API
    void setSampleRate(const int direction, const size_t channel, const double rate) override;
    double getSampleRate(const int direction, const size_t channel) const override;
    std::vector<double> listSampleRates(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getSampleRateRange(const int direction, const size_t channel) const override;

    // Bandwidth API
    void setBandwidth(const int direction, const size_t channel, const double bw) override;
    double getBandwidth(const int direction, const size_t channel) const override;
    std::vector<double> listBandwidths(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getBandwidthRange(const int direction, const size_t channel) const override;

    // Settings API
    SoapySDR::ArgInfoList getSettingInfo() const override;
    void writeSetting(const std::string& key, const std::string& value) override;
    std::string readSetting(const std::string& key) const override;

private:
    // Ensure worker is running
    void ensureWorker();

    // Restart worker process (for recovery from stalls)
    void restartWorker();

    // Send command and wait for response
    bool sendCommand(const IPCMessage& cmd, unsigned int timeoutMs = 5000);

    // Wait for specific status type
    bool waitForStatus(IPCMessageType expectedType, unsigned int timeoutMs = 5000);

    // Device arguments
    SoapySDR::Kwargs deviceArgs_;
    std::string serial_;

    // Worker process
    pid_t workerPid_ = -1;
    std::unique_ptr<IPCPipePair> pipes_;

    // Shared memory
    std::unique_ptr<SharedRingBuffer> ringBuffer_;
    std::string shmName_;

    // Cached settings
    mutable double centerFreq_ = 100e6;
    mutable double sampleRate_ = 2e6;
    mutable double bandwidth_ = 0;
    mutable double gain_ = 40;
    mutable bool agcEnabled_ = true;
    mutable std::string antenna_;
    mutable bool dcOffsetMode_ = true;

    // State
    std::atomic<bool> workerReady_{false};
    std::atomic<bool> streamActive_{false};
};

// Proxy stream handle
struct SoapySDRPlayProxyStream
{
    SharedRingBuffer* ringBuffer;
    size_t lastReadIdx;
    uint64_t lastOverflowCount;
    bool useCS16;  // True if output should be CS16, false for CF32
    std::vector<std::complex<float>> conversionBuffer;  // Buffer for CF32 to CS16 conversion

    // Worker health monitoring
    uint64_t lastSeenWriteIdx = 0;      // Last write index observed
    int staleWriteCount = 0;            // Consecutive reads with no progress
    static constexpr int MAX_STALE_READS = 10;  // Restart worker after this many stale reads
};
