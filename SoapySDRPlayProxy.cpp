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

#include "SoapySDRPlayProxy.hpp"
#include "SDRplayLock.hpp"
#include <SoapySDR/Logger.h>
#include <SoapySDR/Formats.hpp>

#include <stdexcept>
#include <cstring>

// Global cross-process lock for serializing device opening
// The SDRplay API service can't handle concurrent device selection reliably
static SDRplayLock g_proxyDeviceOpenLock("/tmp/soapy_sdrplay_proxy.lock");

SoapySDRPlayProxy::SoapySDRPlayProxy(const SoapySDR::Kwargs& args)
    : deviceArgs_(args)
{
    serial_ = args.count("serial") ? args.at("serial") : "";
    shmName_ = generateShmName(serial_);

    SoapySDR_logf(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Creating proxy for device %s",
                 serial_.c_str());
}

SoapySDRPlayProxy::~SoapySDRPlayProxy()
{
    if (streamActive_)
    {
        // Send stop command
        IPCMessage cmd(IPCMessageType::CMD_STOP);
        sendCommand(cmd);
    }

    if (workerPid_ > 0)
    {
        // Send shutdown command
        IPCMessage cmd(IPCMessageType::CMD_SHUTDOWN);
        if (pipes_ && pipes_->parentToChild())
        {
            pipes_->parentToChild()->send(cmd, 1000);
        }

        WorkerSpawner::terminate(workerPid_);
        workerPid_ = -1;
    }

    ringBuffer_.reset();
    pipes_.reset();

    SoapySDR_logf(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Destroyed");
}

void SoapySDRPlayProxy::ensureWorker()
{
    if (workerReady_)
    {
        return;
    }

    if (workerPid_ > 0)
    {
        // Worker exists but not ready - wait for it
        if (pipes_ && WorkerSpawner::waitForReady(pipes_->childToParent(), 10000))
        {
            workerReady_ = true;
            return;
        }
        // Worker died or timed out
        WorkerSpawner::terminate(workerPid_);
        workerPid_ = -1;
    }

    // Create shared memory
    ringBuffer_.reset(SharedRingBuffer::create(shmName_));
    if (!ringBuffer_)
    {
        throw std::runtime_error("Failed to create shared memory");
    }

    // Spawn worker
    IPCPipePair* pipesPtr = nullptr;
    workerPid_ = WorkerSpawner::spawn(deviceArgs_, shmName_, &pipesPtr);
    if (workerPid_ < 0)
    {
        ringBuffer_.reset();
        throw std::runtime_error("Failed to spawn worker process");
    }

    pipes_.reset(pipesPtr);

    // Wait for worker to be ready
    if (!WorkerSpawner::waitForReady(pipes_->childToParent(), 10000))
    {
        WorkerSpawner::terminate(workerPid_);
        workerPid_ = -1;
        ringBuffer_.reset();
        pipes_.reset();
        throw std::runtime_error("Worker failed to start");
    }

    workerReady_ = true;
    SoapySDR_logf(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Worker ready for device %s",
                 serial_.c_str());
}

void SoapySDRPlayProxy::restartWorker()
{
    bool wasStreaming = streamActive_.load();

    // Mark worker as not ready
    workerReady_ = false;
    streamActive_ = false;

    // Terminate existing worker
    if (workerPid_ > 0)
    {
        SoapySDR_logf(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Terminating stalled worker PID %d",
                     workerPid_);
        WorkerSpawner::terminate(workerPid_);
        workerPid_ = -1;
    }

    // Close old pipes
    pipes_.reset();

    // Recreate shared memory (clears buffer)
    ringBuffer_.reset();

    try
    {
        // Spawn new worker - ensureWorker handles all the spawning logic
        ensureWorker();

        // Send CMD_CONFIGURE first to open device (same as setupStream does)
        SoapySDR_log(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Configuring device in restarted worker");
        IPCMessage configCmd(IPCMessageType::CMD_CONFIGURE);
        configCmd.setParam("center_hz", centerFreq_);
        configCmd.setParam("sample_rate", sampleRate_);
        configCmd.setParam("bandwidth", bandwidth_);
        configCmd.setParam("gain", gain_);
        configCmd.setParam("agc", static_cast<int64_t>(agcEnabled_ ? 1 : 0));
        configCmd.setParam("antenna", antenna_);

        if (!sendCommand(configCmd))
        {
            throw std::runtime_error("Failed to send configure command to restarted worker");
        }

        if (!waitForStatus(IPCMessageType::STATUS_CONFIGURED, 15000))
        {
            throw std::runtime_error("Configure failed in restarted worker");
        }

        // Restart streaming if it was active
        if (wasStreaming)
        {
            SoapySDR_log(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Restarting stream");
            IPCMessage startCmd(IPCMessageType::CMD_START);
            if (sendCommand(startCmd) && waitForStatus(IPCMessageType::STATUS_STARTED, 10000))
            {
                streamActive_ = true;
            }
            else
            {
                SoapySDR_log(SOAPY_SDR_WARNING, "SoapySDRPlayProxy: Failed to restart stream after worker recovery");
            }
        }

        SoapySDR_log(SOAPY_SDR_INFO, "SoapySDRPlayProxy: Worker restart complete");
    }
    catch (const std::exception& e)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SoapySDRPlayProxy: Worker restart failed: %s", e.what());
        throw;
    }
}

bool SoapySDRPlayProxy::sendCommand(const IPCMessage& cmd, unsigned int timeoutMs)
{
    if (!pipes_ || !pipes_->parentToChild())
    {
        return false;
    }

    if (!pipes_->parentToChild()->send(cmd, timeoutMs))
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SoapySDRPlayProxy: Failed to send command");
        return false;
    }

    return true;
}

bool SoapySDRPlayProxy::waitForStatus(IPCMessageType expectedType, unsigned int timeoutMs)
{
    if (!pipes_ || !pipes_->childToParent())
    {
        return false;
    }

    auto startTime = std::chrono::steady_clock::now();

    while (true)
    {
        // Calculate remaining timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed >= timeoutMs)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SoapySDRPlayProxy: Timeout waiting for status %d",
                         static_cast<int>(expectedType));
            return false;
        }

        unsigned int remainingMs = static_cast<unsigned int>(timeoutMs - elapsed);

        IPCMessage status;
        if (!pipes_->childToParent()->receive(status, remainingMs))
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SoapySDRPlayProxy: Timeout waiting for status %d",
                         static_cast<int>(expectedType));
            return false;
        }

        if (status.type == IPCMessageType::STATUS_ERROR)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SoapySDRPlayProxy: Worker error: %s",
                         status.getParam("message").c_str());
            return false;
        }

        if (status.type == expectedType || status.type == IPCMessageType::STATUS_ACK)
        {
            return true;
        }

        // Got unexpected status - log and try again
        SoapySDR_logf(SOAPY_SDR_DEBUG, "SoapySDRPlayProxy: Discarding unexpected status %d while waiting for %d",
                     static_cast<int>(status.type), static_cast<int>(expectedType));
    }
}

// Identification API

std::string SoapySDRPlayProxy::getDriverKey() const
{
    return "sdrplay";
}

std::string SoapySDRPlayProxy::getHardwareKey() const
{
    return "RSP";
}

SoapySDR::Kwargs SoapySDRPlayProxy::getHardwareInfo() const
{
    SoapySDR::Kwargs info;
    info["serial"] = serial_;
    info["proxy"] = "true";
    return info;
}

// Channels API

size_t SoapySDRPlayProxy::getNumChannels(const int direction) const
{
    return (direction == SOAPY_SDR_RX) ? 1 : 0;
}

SoapySDR::Kwargs SoapySDRPlayProxy::getChannelInfo(const int /* direction */, const size_t /* channel */) const
{
    return SoapySDR::Kwargs();
}

// Stream API

std::vector<std::string> SoapySDRPlayProxy::getStreamFormats(const int /* direction */, const size_t /* channel */) const
{
    return { SOAPY_SDR_CF32, SOAPY_SDR_CS16 };
}

std::string SoapySDRPlayProxy::getNativeStreamFormat(const int /* direction */, const size_t /* channel */, double& fullScale) const
{
    fullScale = 1.0;
    return SOAPY_SDR_CF32;
}

SoapySDR::ArgInfoList SoapySDRPlayProxy::getStreamArgsInfo(const int /* direction */, const size_t /* channel */) const
{
    return SoapySDR::ArgInfoList();
}

SoapySDR::Stream* SoapySDRPlayProxy::setupStream(
    const int direction,
    const std::string& format,
    const std::vector<size_t>& /* channels */,
    const SoapySDR::Kwargs& /* args */)
{
    if (direction != SOAPY_SDR_RX)
    {
        throw std::runtime_error("Only RX streams are supported");
    }

    bool useCS16 = false;
    if (format == SOAPY_SDR_CS16)
    {
        useCS16 = true;
    }
    else if (format != SOAPY_SDR_CF32)
    {
        throw std::runtime_error("Only CF32 and CS16 formats are supported in proxy mode");
    }

    // Serialize device opening across all proxy instances
    // The SDRplay API service can't handle concurrent device selection reliably
    {
        SoapySDR_log(SOAPY_SDR_DEBUG, "SoapySDRPlayProxy: Acquiring device open lock...");
        SDRplayLockGuard openLock(g_proxyDeviceOpenLock, 60000, 0);  // 60s timeout, no cooldown
        SoapySDR_log(SOAPY_SDR_DEBUG, "SoapySDRPlayProxy: Device open lock acquired");

        ensureWorker();

        // Send configure command
        IPCMessage cmd(IPCMessageType::CMD_CONFIGURE);
        cmd.setParam("center_hz", centerFreq_);
        cmd.setParam("sample_rate", sampleRate_);
        cmd.setParam("bandwidth", bandwidth_);
        cmd.setParam("gain", gain_);
        cmd.setParam("agc", static_cast<int64_t>(agcEnabled_ ? 1 : 0));
        cmd.setParam("antenna", antenna_);

        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send configure command");
        }

        if (!waitForStatus(IPCMessageType::STATUS_CONFIGURED, 15000))
        {
            throw std::runtime_error("Configure failed");
        }

        SoapySDR_log(SOAPY_SDR_DEBUG, "SoapySDRPlayProxy: Device configured, releasing lock");
    }  // Lock released here - device is fully configured

    auto* stream = new SoapySDRPlayProxyStream();
    stream->ringBuffer = ringBuffer_.get();
    stream->lastReadIdx = 0;
    stream->lastOverflowCount = 0;
    stream->useCS16 = useCS16;

    // Pre-allocate conversion buffer to MTU size to avoid repeated reallocations
    if (useCS16)
    {
        stream->conversionBuffer.reserve(getStreamMTU(nullptr));
    }

    return reinterpret_cast<SoapySDR::Stream*>(stream);
}

void SoapySDRPlayProxy::closeStream(SoapySDR::Stream* stream)
{
    if (streamActive_)
    {
        deactivateStream(stream);
    }

    auto* proxyStream = reinterpret_cast<SoapySDRPlayProxyStream*>(stream);

    // Release conversion buffer memory before deletion
    proxyStream->conversionBuffer.clear();
    proxyStream->conversionBuffer.shrink_to_fit();

    delete proxyStream;
}

size_t SoapySDRPlayProxy::getStreamMTU(SoapySDR::Stream* /* stream */) const
{
    return 65536;
}

int SoapySDRPlayProxy::activateStream(SoapySDR::Stream* /* stream */, const int /* flags */, const long long /* timeNs */, const size_t /* numElems */)
{
    IPCMessage cmd(IPCMessageType::CMD_START);
    if (!sendCommand(cmd))
    {
        return SOAPY_SDR_STREAM_ERROR;
    }

    if (!waitForStatus(IPCMessageType::STATUS_STARTED, 10000))
    {
        return SOAPY_SDR_STREAM_ERROR;
    }

    streamActive_ = true;
    return 0;
}

int SoapySDRPlayProxy::deactivateStream(SoapySDR::Stream* /* stream */, const int /* flags */, const long long /* timeNs */)
{
    if (!streamActive_)
    {
        return 0;
    }

    IPCMessage cmd(IPCMessageType::CMD_STOP);
    if (!sendCommand(cmd))
    {
        return SOAPY_SDR_STREAM_ERROR;
    }

    waitForStatus(IPCMessageType::STATUS_STOPPED, 5000);
    streamActive_ = false;
    return 0;
}

int SoapySDRPlayProxy::readStream(
    SoapySDR::Stream* stream,
    void* const* buffs,
    const size_t numElems,
    int& flags,
    long long& timeNs,
    const long timeoutUs)
{
    auto* proxyStream = reinterpret_cast<SoapySDRPlayProxyStream*>(stream);
    auto* buffer = proxyStream->ringBuffer;

    flags = 0;
    timeNs = 0;

    size_t count;

    if (proxyStream->useCS16)
    {
        // Read into conversion buffer, then convert CF32 -> CS16
        if (proxyStream->conversionBuffer.size() < numElems)
        {
            proxyStream->conversionBuffer.resize(numElems);
        }
        else if (proxyStream->conversionBuffer.capacity() > numElems * 4 &&
                 proxyStream->conversionBuffer.capacity() > 65536)
        {
            // Shrink if capacity is 4x larger than needed and exceeds default MTU
            // This prevents unbounded memory growth from occasional large reads
            proxyStream->conversionBuffer.resize(numElems);
            proxyStream->conversionBuffer.shrink_to_fit();
        }

        count = buffer->read(
            proxyStream->conversionBuffer.data(),
            numElems,
            timeoutUs
        );

        if (count > 0)
        {
            // Convert CF32 to CS16 (scale by 32767)
            auto* output = reinterpret_cast<int16_t*>(buffs[0]);
            for (size_t i = 0; i < count; i++)
            {
                const auto& sample = proxyStream->conversionBuffer[i];
                // Clamp and scale to int16 range
                float re = sample.real() * 32767.0f;
                float im = sample.imag() * 32767.0f;
                re = std::max(-32768.0f, std::min(32767.0f, re));
                im = std::max(-32768.0f, std::min(32767.0f, im));
                output[2 * i] = static_cast<int16_t>(re);
                output[2 * i + 1] = static_cast<int16_t>(im);
            }
        }
    }
    else
    {
        // Read directly as CF32
        count = buffer->read(
            reinterpret_cast<std::complex<float>*>(buffs[0]),
            numElems,
            timeoutUs
        );
    }

    if (count == 0)
    {
        // Check if worker is still making progress
        uint64_t currentWriteIdx = buffer->writeIndex();
        if (currentWriteIdx == proxyStream->lastSeenWriteIdx)
        {
            proxyStream->staleWriteCount++;
            if (proxyStream->staleWriteCount >= SoapySDRPlayProxyStream::MAX_STALE_READS)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING,
                    "SoapySDRPlayProxy: Ring buffer stalled (write index %llu unchanged for %d reads), restarting worker",
                    (unsigned long long)currentWriteIdx, proxyStream->staleWriteCount);
                restartWorker();
                // Update stream's buffer pointer since restartWorker creates a new buffer
                proxyStream->ringBuffer = ringBuffer_.get();
                proxyStream->staleWriteCount = 0;
                proxyStream->lastSeenWriteIdx = 0;
            }
        }
        else
        {
            proxyStream->lastSeenWriteIdx = currentWriteIdx;
            proxyStream->staleWriteCount = 0;
        }
        return SOAPY_SDR_TIMEOUT;
    }

    // Reset stale tracking on successful read
    proxyStream->staleWriteCount = 0;

    // Check for overflow
    uint64_t currentOverflow = buffer->overflowCount();
    if (currentOverflow > proxyStream->lastOverflowCount)
    {
        flags |= SOAPY_SDR_HAS_TIME;  // Use as overflow indicator
        proxyStream->lastOverflowCount = currentOverflow;
    }

    return static_cast<int>(count);
}

// Direct buffer access API

size_t SoapySDRPlayProxy::getNumDirectAccessBuffers(SoapySDR::Stream* /* stream */)
{
    return 1;
}

int SoapySDRPlayProxy::getDirectAccessBufferAddrs(SoapySDR::Stream* /* stream */, const size_t /* handle */, void** /* buffs */)
{
    return SOAPY_SDR_NOT_SUPPORTED;
}

int SoapySDRPlayProxy::acquireReadBuffer(SoapySDR::Stream* stream, size_t& handle, const void** buffs, int& flags, long long& timeNs, const long timeoutUs)
{
    auto* proxyStream = reinterpret_cast<SoapySDRPlayProxyStream*>(stream);
    auto* buffer = proxyStream->ringBuffer;

    flags = 0;
    timeNs = 0;
    handle = 0;

    // Get direct pointer to ring buffer
    size_t available = 0;
    const std::complex<float>* ptr = buffer->getReadPtr(&available);

    if (!ptr || available == 0)
    {
        // Wait for data
        auto start = std::chrono::steady_clock::now();
        while (available == 0)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutUs)
            {
                return SOAPY_SDR_TIMEOUT;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            ptr = buffer->getReadPtr(&available);
        }
    }

    buffs[0] = ptr;
    return static_cast<int>(available);
}

void SoapySDRPlayProxy::releaseReadBuffer(SoapySDR::Stream* stream, const size_t handle)
{
    (void)handle;  // We only have one buffer
    auto* proxyStream = reinterpret_cast<SoapySDRPlayProxyStream*>(stream);

    // This should advance by the amount actually consumed
    // For now, assume full buffer was consumed
    size_t available = 0;
    proxyStream->ringBuffer->getReadPtr(&available);
    proxyStream->ringBuffer->advanceRead(available);
}

// Antenna API

std::vector<std::string> SoapySDRPlayProxy::listAntennas(const int /* direction */, const size_t /* channel */) const
{
    return { "Antenna A", "Antenna B", "Hi-Z" };
}

void SoapySDRPlayProxy::setAntenna(const int /* direction */, const size_t /* channel */, const std::string& name)
{
    antenna_ = name;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_ANTENNA);
        cmd.setParam("value", name);
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setAntenna command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setAntenna command failed or timed out");
        }
    }
}

std::string SoapySDRPlayProxy::getAntenna(const int /* direction */, const size_t /* channel */) const
{
    return antenna_;
}

// DC Offset Mode API

bool SoapySDRPlayProxy::hasDCOffsetMode(const int /* direction */, const size_t /* channel */) const
{
    return true;
}

void SoapySDRPlayProxy::setDCOffsetMode(const int /* direction */, const size_t /* channel */, const bool automatic)
{
    dcOffsetMode_ = automatic;
}

bool SoapySDRPlayProxy::getDCOffsetMode(const int /* direction */, const size_t /* channel */) const
{
    return dcOffsetMode_;
}

// Gain API

std::vector<std::string> SoapySDRPlayProxy::listGains(const int /* direction */, const size_t /* channel */) const
{
    return { "IFGR", "RFGR" };
}

bool SoapySDRPlayProxy::hasGainMode(const int /* direction */, const size_t /* channel */) const
{
    return true;
}

void SoapySDRPlayProxy::setGainMode(const int /* direction */, const size_t /* channel */, const bool automatic)
{
    agcEnabled_ = automatic;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_AGC);
        cmd.setParam("value", static_cast<int64_t>(automatic ? 1 : 0));
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setGainMode command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setGainMode command failed or timed out");
        }
    }
}

bool SoapySDRPlayProxy::getGainMode(const int /* direction */, const size_t /* channel */) const
{
    return agcEnabled_;
}

void SoapySDRPlayProxy::setGain(const int /* direction */, const size_t /* channel */, const double value)
{
    gain_ = value;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_GAIN);
        cmd.setParam("value", value);
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setGain command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setGain command failed or timed out");
        }
    }
}

void SoapySDRPlayProxy::setGain(const int direction, const size_t channel, const std::string& /* name */, const double value)
{
    setGain(direction, channel, value);
}

double SoapySDRPlayProxy::getGain(const int /* direction */, const size_t /* channel */) const
{
    return gain_;
}

double SoapySDRPlayProxy::getGain(const int /* direction */, const size_t /* channel */, const std::string& /* name */) const
{
    return gain_;
}

SoapySDR::Range SoapySDRPlayProxy::getGainRange(const int /* direction */, const size_t /* channel */) const
{
    return SoapySDR::Range(0, 59);
}

SoapySDR::Range SoapySDRPlayProxy::getGainRange(const int /* direction */, const size_t /* channel */, const std::string& /* name */) const
{
    return SoapySDR::Range(0, 59);
}

// Frequency API

void SoapySDRPlayProxy::setFrequency(const int /* direction */, const size_t /* channel */, const double frequency, const SoapySDR::Kwargs& /* args */)
{
    centerFreq_ = frequency;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_FREQUENCY);
        cmd.setParam("value", frequency);
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setFrequency command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setFrequency command failed or timed out");
        }
    }
}

void SoapySDRPlayProxy::setFrequency(const int direction, const size_t channel, const std::string& /* name */, const double frequency, const SoapySDR::Kwargs& args)
{
    setFrequency(direction, channel, frequency, args);
}

double SoapySDRPlayProxy::getFrequency(const int /* direction */, const size_t /* channel */) const
{
    return centerFreq_;
}

double SoapySDRPlayProxy::getFrequency(const int /* direction */, const size_t /* channel */, const std::string& /* name */) const
{
    return centerFreq_;
}

std::vector<std::string> SoapySDRPlayProxy::listFrequencies(const int /* direction */, const size_t /* channel */) const
{
    return { "RF" };
}

SoapySDR::RangeList SoapySDRPlayProxy::getFrequencyRange(const int /* direction */, const size_t /* channel */) const
{
    return { SoapySDR::Range(1e3, 2e9) };
}

SoapySDR::RangeList SoapySDRPlayProxy::getFrequencyRange(const int /* direction */, const size_t /* channel */, const std::string& /* name */) const
{
    return { SoapySDR::Range(1e3, 2e9) };
}

SoapySDR::ArgInfoList SoapySDRPlayProxy::getFrequencyArgsInfo(const int /* direction */, const size_t /* channel */) const
{
    return SoapySDR::ArgInfoList();
}

// Sample Rate API

void SoapySDRPlayProxy::setSampleRate(const int /* direction */, const size_t /* channel */, const double rate)
{
    sampleRate_ = rate;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_SAMPLE_RATE);
        cmd.setParam("value", rate);
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setSampleRate command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setSampleRate command failed or timed out");
        }
    }
}

double SoapySDRPlayProxy::getSampleRate(const int /* direction */, const size_t /* channel */) const
{
    return sampleRate_;
}

std::vector<double> SoapySDRPlayProxy::listSampleRates(const int /* direction */, const size_t /* channel */) const
{
    return { 62500, 96000, 125000, 192000, 250000, 500000, 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000 };
}

SoapySDR::RangeList SoapySDRPlayProxy::getSampleRateRange(const int /* direction */, const size_t /* channel */) const
{
    return { SoapySDR::Range(62500, 10000000) };
}

// Bandwidth API

void SoapySDRPlayProxy::setBandwidth(const int /* direction */, const size_t /* channel */, const double bw)
{
    bandwidth_ = bw;

    if (workerReady_)
    {
        IPCMessage cmd(IPCMessageType::CMD_SET_BANDWIDTH);
        cmd.setParam("value", bw);
        if (!sendCommand(cmd))
        {
            throw std::runtime_error("Failed to send setBandwidth command to worker");
        }
        if (!waitForStatus(IPCMessageType::STATUS_ACK, 5000))
        {
            throw std::runtime_error("setBandwidth command failed or timed out");
        }
    }
}

double SoapySDRPlayProxy::getBandwidth(const int /* direction */, const size_t /* channel */) const
{
    return bandwidth_;
}

std::vector<double> SoapySDRPlayProxy::listBandwidths(const int /* direction */, const size_t /* channel */) const
{
    return { 200000, 300000, 600000, 1536000, 5000000, 6000000, 7000000, 8000000 };
}

SoapySDR::RangeList SoapySDRPlayProxy::getBandwidthRange(const int /* direction */, const size_t /* channel */) const
{
    return { SoapySDR::Range(200000, 8000000) };
}

// Settings API

SoapySDR::ArgInfoList SoapySDRPlayProxy::getSettingInfo() const
{
    return SoapySDR::ArgInfoList();
}

void SoapySDRPlayProxy::writeSetting(const std::string& /* key */, const std::string& /* value */)
{
    // Settings not yet implemented in proxy mode
}

std::string SoapySDRPlayProxy::readSetting(const std::string& /* key */) const
{
    return "";
}
