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
#include <iostream>

std::vector<std::string> SoapySDRPlay::getStreamFormats(const int direction, const size_t channel) const
{
    std::vector<std::string> formats;

    formats.push_back("CS16");
    formats.push_back("CF32");

    return formats;
}

std::string SoapySDRPlay::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
     fullScale = 32767;
     return "CS16";
}

SoapySDR::ArgInfoList SoapySDRPlay::getStreamArgsInfo(const int direction, const size_t channel) const
{
    SoapySDR::ArgInfoList streamArgs;

    return streamArgs;
}

/*******************************************************************
 * Async thread work
 ******************************************************************/

static void _rx_callback_A(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params,
                           unsigned int numSamples, unsigned int reset, void *cbContext)
{
    auto *self = static_cast<SoapySDRPlay *>(cbContext);
    SoapySDRPlay::SoapySDRPlayStream *stream = nullptr;
    std::unique_lock<std::mutex> streamLock;
    {
        std::lock_guard<std::mutex> lock(self->_streams_mutex);
        stream = self->_streams[0];
        if (stream == nullptr) {
            return;
        }
        streamLock = std::unique_lock<std::mutex>(stream->mutex);
    }
    return self->rx_callback(xi, xq, params, numSamples, stream);
}

static void _rx_callback_B(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params,
                           unsigned int numSamples, unsigned int reset, void *cbContext)
{
    auto *self = static_cast<SoapySDRPlay *>(cbContext);
    SoapySDRPlay::SoapySDRPlayStream *stream = nullptr;
    std::unique_lock<std::mutex> streamLock;
    {
        std::lock_guard<std::mutex> lock(self->_streams_mutex);
        stream = self->_streams[1];
        if (stream == nullptr) {
            return;
        }
        streamLock = std::unique_lock<std::mutex>(stream->mutex);
    }
    return self->rx_callback(xi, xq, params, numSamples, stream);
}

static void _ev_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner,
                         sdrplay_api_EventParamsT *params, void *cbContext)
{
    auto *self = static_cast<SoapySDRPlay *>(cbContext);
    return self->ev_callback(eventId, tuner, params);
}

void SoapySDRPlay::rx_callback(short *xi, short *xq,
                               sdrplay_api_StreamCbParamsT *params,
                               unsigned int numSamples,
                               SoapySDRPlayStream *stream)
{
    // stream->mutex must be held by the caller to prevent concurrent teardown
    if (stream == nullptr || device_unavailable) {
        return;
    }

    // Defensive check for null input pointers from SDRplay API
    if (xi == nullptr || xq == nullptr) {
        SoapySDR_log(SOAPY_SDR_WARNING, "rx_callback: null input pointer from SDRplay API");
        return;
    }

    // Track callback activity for stale callback detection
    stream->lastCallbackTicks.fetch_add(1, std::memory_order_relaxed);

    bool notify = false;
    if (gr_changed == 0 && params->grChanged != 0)
    {
        gr_changed = params->grChanged;
        notify = true;
    }
    if (rf_changed == 0 && params->rfChanged != 0)
    {
        rf_changed = params->rfChanged;
        notify = true;
    }
    if (fs_changed == 0 && params->fsChanged != 0)
    {
        fs_changed = params->fsChanged;
        notify = true;
    }
    if (notify)
    {
        update_cv.notify_all();
    }

    if (stream->count == numBuffers)
    {
        stream->overflowEvent = true;
        return;
    }

    const size_t spaceReqd = static_cast<size_t>(numSamples) * elementsPerSample;
    // Use cached threshold to avoid division in hot path
    size_t threshold = static_cast<size_t>(cachedBufferThreshold.load(std::memory_order_relaxed));
    if (threshold == 0) threshold = static_cast<size_t>(bufferLength.load());  // Fallback if not yet initialized

    // copy into the buffer queue
    unsigned int i = 0;

    if (useShort)
    {
        {
            auto &buff = stream->shortBuffs[stream->tail];
            if ((buff.size() + spaceReqd) >= threshold)
            {
                // increment the tail pointer and buffer count
                // Use bitwise AND instead of modulo for power-of-2 numBuffers (faster)
                stream->tail = (stream->tail + 1) & (numBuffers - 1);
                stream->count++;

                auto &nextBuff = stream->shortBuffs[stream->tail];
                if (stream->count == numBuffers && spaceReqd > nextBuff.capacity() - nextBuff.size())
                {
                    stream->overflowEvent = true;
                    return;
                }

                // notify readStream()
                stream->cond.notify_one();
            }
        }

        // get current fill buffer
        auto &buff = stream->shortBuffs[stream->tail];

        // Check if resize would exceed capacity (would cause reallocation)
        size_t newSize = buff.size() + spaceReqd;
        if (newSize > buff.capacity())
        {
            stream->overflowEvent = true;
            return;
        }

        // resize within pre-allocated capacity (no reallocation)
        buff.resize(newSize);

        short *dptr = buff.data();
        dptr += (buff.size() - spaceReqd);
        for (i = 0; i < numSamples; i++)
        {
            *dptr++ = xi[i];
            *dptr++ = xq[i];
        }
    }
    else
    {
        {
            auto &buff = stream->floatBuffs[stream->tail];
            if ((buff.size() + spaceReqd) >= threshold)
            {
                // increment the tail pointer and buffer count
                // Use bitwise AND instead of modulo for power-of-2 numBuffers (faster)
                stream->tail = (stream->tail + 1) & (numBuffers - 1);
                stream->count++;

                auto &nextBuff = stream->floatBuffs[stream->tail];
                if (stream->count == numBuffers && spaceReqd > nextBuff.capacity() - nextBuff.size())
                {
                    stream->overflowEvent = true;
                    return;
                }

                // notify readStream()
                stream->cond.notify_one();
            }
        }

        // get current fill buffer
        auto &buff = stream->floatBuffs[stream->tail];

        // Check if resize would exceed capacity (would cause reallocation)
        size_t newSize = buff.size() + spaceReqd;
        if (newSize > buff.capacity())
        {
            stream->overflowEvent = true;
            return;
        }

        // resize within pre-allocated capacity (no reallocation)
        buff.resize(newSize);

        // Use multiplication by reciprocal instead of division for performance
        // (multiplication is faster than division in the hot path)
        constexpr float SCALE = 1.0f / 32768.0f;
        float *dptr = buff.data();
        dptr += (buff.size() - spaceReqd);
        for (i = 0; i < numSamples; i++)
        {
            *dptr++ = static_cast<float>(xi[i]) * SCALE;
            *dptr++ = static_cast<float>(xq[i]) * SCALE;
        }
    }

    return;
}

void SoapySDRPlay::ev_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params)
{
    if (eventId == sdrplay_api_GainChange)
    {
        //Beware, lnaGRdB is really the LNA GR, NOT the LNA state !
        //sdrplay_api_GainCbParamT gainParams = params->gainParams;
        //unsigned int gRdB = gainParams.gRdB;
        //unsigned int lnaGRdB = gainParams.lnaGRdB;
        // gainParams.currGain is a calibrated gain value
        //if (gRdB < 200)
        //{
        //    current_gRdB = gRdB;
        //}
    }
    else if (eventId == sdrplay_api_PowerOverloadChange)
    {
        sdrplay_api_PowerOverloadCbEventIdT powerOverloadChangeType = params->powerOverloadParams.powerOverloadChangeType;
        if (powerOverloadChangeType == sdrplay_api_Overload_Detected)
        {
            SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Ctrl_OverloadMsgAck) failed: %s", sdrplay_api_GetErrorString(err));
            }
            // OVERLOAD DETECTED
        }
        else if (powerOverloadChangeType == sdrplay_api_Overload_Corrected)
        {
            SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Ctrl_OverloadMsgAck) failed: %s", sdrplay_api_GetErrorString(err));
            }
            // OVERLOAD CORRECTED
        }
    }
    else if (eventId == sdrplay_api_DeviceRemoved)
    {
        // Notify readStream() that the device has been removed so that
        // the application can be closed gracefully
        SoapySDR_log(SOAPY_SDR_ERROR, "Device has been removed. Stopping.");
        device_unavailable = true;
        // Wake up any waiting threads so they can exit gracefully
        update_cv.notify_all();
        // Safely access stream pointers under lock to prevent use-after-free
        {
            std::lock_guard<std::mutex> lock(_streams_mutex);
            if (_streams[0]) _streams[0]->cond.notify_all();
            if (_streams[1]) _streams[1]->cond.notify_all();
        }
    }
    else if (eventId == sdrplay_api_RspDuoModeChange)
    {
        sdrplay_api_RspDuoModeCbEventIdT modeChangeType = params->rspDuoModeParams.modeChangeType;

        // Log all RSPduo mode change events for diagnostics
        switch (modeChangeType)
        {
            case sdrplay_api_MasterInitialised:
                SoapySDR_log(SOAPY_SDR_INFO, "RSPduo: Master initialised");
                break;
            case sdrplay_api_SlaveAttached:
                SoapySDR_log(SOAPY_SDR_INFO, "RSPduo: Slave attached");
                break;
            case sdrplay_api_SlaveDetached:
                SoapySDR_log(SOAPY_SDR_INFO, "RSPduo: Slave detached");
                break;
            case sdrplay_api_SlaveInitialised:
                SoapySDR_log(SOAPY_SDR_INFO, "RSPduo: Slave initialised");
                break;
            case sdrplay_api_SlaveUninitialised:
                SoapySDR_log(SOAPY_SDR_INFO, "RSPduo: Slave uninitialised");
                break;
            case sdrplay_api_MasterDllDisappeared:
                // Critical: Master stream has been removed - must stop gracefully
                SoapySDR_log(SOAPY_SDR_ERROR, "RSPduo: Master stream has disappeared. Stopping.");
                device_unavailable = true;
                update_cv.notify_all();
                {
                    std::lock_guard<std::mutex> lock(_streams_mutex);
                    if (_streams[0]) _streams[0]->cond.notify_all();
                    if (_streams[1]) _streams[1]->cond.notify_all();
                }
                break;
            case sdrplay_api_SlaveDllDisappeared:
                // Slave stream has been removed - log but continue (master can survive)
                SoapySDR_log(SOAPY_SDR_WARNING, "RSPduo: Slave stream has disappeared");
                break;
            default:
                SoapySDR_logf(SOAPY_SDR_WARNING, "RSPduo: Unknown mode change event (%d)", static_cast<int>(modeChangeType));
                break;
        }
    }
}

/*******************************************************************
 * Stream API
 ******************************************************************/

SoapySDRPlay::SoapySDRPlayStream::SoapySDRPlayStream(size_t channel,
                                                     size_t numBuffers,
                                                     unsigned long bufferLength)
{
    std::lock_guard<std::mutex> lock(mutex);

    this->channel = channel;

    // clear async fifo counts
    tail = 0;
    head = 0;
    count = 0;

    // initialize other members
    currentBuff = nullptr;
    overflowEvent = false;
    nElems = 0;
    currentHandle = 0;
    reset = false;

    // allocate buffers
    shortBuffs.resize(numBuffers);
    for (auto &buff : shortBuffs) buff.reserve(bufferLength);
    floatBuffs.resize(numBuffers);
    for (auto &buff : floatBuffs) buff.reserve(bufferLength);
}

SoapySDRPlay::SoapySDRPlayStream::~SoapySDRPlayStream()
{
}

SoapySDR::Stream *SoapySDRPlay::setupStream(const int direction,
                                            const std::string &format,
                                            const std::vector<size_t> &channels,
                                            const SoapySDR::Kwargs &args)
{
    // Prevent format changes while streaming is active
    if (streamActive)
    {
        throw std::runtime_error("setupStream cannot be called while streaming is active");
    }

    size_t nchannels = device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner ? 2 : 1;

    // check the channel configuration
    if (channels.size() > 1 || (channels.size() > 0 && channels.at(0) >= nchannels))
    {
       throw std::runtime_error("setupStream invalid channel selection");
    }

    // check the format
    if (format == "CS16")
    {
        useShort = true;
        bufferLength = bufferElems * elementsPerSample;
        SoapySDR_log(SOAPY_SDR_INFO, "Using format CS16.");
    }
    else if (format == "CF32")
    {
        useShort = false;
        bufferLength = bufferElems * elementsPerSample;
        SoapySDR_log(SOAPY_SDR_INFO, "Using format CF32.");
    }
    else
    {
        throw std::runtime_error( "setupStream invalid format '" + format +
                                  "' -- Only CS16 or CF32 are supported by the SoapySDRPlay module.");
    }

    // Initialize cached buffer threshold based on current decimation factor
    unsigned int decFactor = chParams->ctrlParams.decimation.decimationFactor;
    if (decFactor == 0) decFactor = 1;
    cachedBufferThreshold = bufferLength / decFactor;

    // default is channel 0
    size_t channel = channels.size() == 0 ? 0 : channels.at(0);
    SoapySDRPlayStream *sdrplay_stream;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        sdrplay_stream = _streams[channel];
    }
    if (sdrplay_stream == nullptr)
    {
        sdrplay_stream = new SoapySDRPlayStream(channel, numBuffers, bufferLength);
    }
    return reinterpret_cast<SoapySDR::Stream *>(sdrplay_stream);
}

void SoapySDRPlay::closeStream(SoapySDR::Stream *stream)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);

    bool deleteStream = false;
    int activeStreams = 0;
    std::unique_lock<std::mutex> streamsLock(_streams_mutex);
    for (int i = 0; i < 2; ++i)
    {
        if (_streams[i] == sdrplay_stream)
        {
            _streamsRefCount[i]--;
            if (_streamsRefCount[i] == 0)
            {
                _streams[i] = nullptr;
                deleteStream = true;
            }
        }
        activeStreams += _streamsRefCount[i];
    }

    if (deleteStream)
    {
        // Wake up any threads waiting on this stream's condition variable.
        // Use notify_all() to ensure all waiters wake up and see the stream is gone.
        // Hold the stream mutex while clearing and notify to avoid concurrent callbacks.
        std::unique_lock<std::mutex> streamLock(sdrplay_stream->mutex);
        streamsLock.unlock();
        sdrplay_stream->cond.notify_all();
        streamLock.unlock();

        // Acquire readStreamMutex to ensure any thread in readStream() has exited.
        // This is safe because we've already set _streams[i] = nullptr above,
        // so new readStream() calls will return early, and existing ones will
        // exit after seeing the stream is gone or timing out.
        {
            std::lock_guard<std::mutex> readLock(sdrplay_stream->readStreamMutex);
        }

        // CRITICAL: Call Uninit BEFORE deleting stream to prevent use-after-free.
        // The SDRplay API callbacks must be stopped before we free the stream memory,
        // otherwise a callback in flight could access freed memory.
        if (activeStreams == 0)
        {
            // Stop watchdog before stopping stream
            stopWatchdog();

            while (true)
            {
                sdrplay_api_ErrT err;
                SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
                err = sdrplay_api_Uninit(device.dev);
                if (err != sdrplay_api_StopPending)
                {
                    break;
                }
                SoapySDR_logf(SOAPY_SDR_WARNING, "Please close RSPduo slave device first. Trying again in %d seconds", uninitRetryDelay);
                std::this_thread::sleep_for(std::chrono::seconds(uninitRetryDelay));
            }
            streamActive = false;
        }

        delete sdrplay_stream;
    }
    else
    {
        streamsLock.unlock();

        // Handle case where we didn't delete a stream but all streams are now inactive
        if (activeStreams == 0)
        {
            // Stop watchdog before stopping stream
            stopWatchdog();

            while (true)
            {
                sdrplay_api_ErrT err;
                SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
                err = sdrplay_api_Uninit(device.dev);
                if (err != sdrplay_api_StopPending)
                {
                    break;
                }
                SoapySDR_logf(SOAPY_SDR_WARNING, "Please close RSPduo slave device first. Trying again in %d seconds", uninitRetryDelay);
                std::this_thread::sleep_for(std::chrono::seconds(uninitRetryDelay));
            }
            streamActive = false;
        }
    }
}

size_t SoapySDRPlay::getStreamMTU(SoapySDR::Stream *stream) const
{
    // is a constant in practice
    return bufferElems;
}

int SoapySDRPlay::activateStream(SoapySDR::Stream *stream,
                                 const int flags,
                                 const long long timeNs,
                                 const size_t numElems)
{
    if (flags != 0)
    {
        SoapySDR_log(SOAPY_SDR_ERROR, "error in activateStream() - flags != 0");
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);

    sdrplay_api_ErrT err;

    std::unique_lock<std::mutex> lock(_general_state_mutex);

    {
        std::lock_guard<std::mutex> streamsLock(_streams_mutex);
        sdrplay_stream->reset = true;
        sdrplay_stream->nElems = 0;
        _streams[sdrplay_stream->channel] = sdrplay_stream;
        _streamsRefCount[sdrplay_stream->channel]++;
    }

    if (streamActive)
    {
        return 0;
    }

    // Enable (= sdrplay_api_DbgLvl_Verbose) API calls tracing,
    // but only for debug purposes due to its performance impact.
    {
        SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
        sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Disable);
    }
    //sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Verbose);

    chParams->tunerParams.dcOffsetTuner.dcCal = 4;
    chParams->tunerParams.dcOffsetTuner.speedUp = 0;
    chParams->tunerParams.dcOffsetTuner.trackTime = 63;

    sdrplay_api_CallbackFnsT cbFns;
    cbFns.StreamACbFn = _rx_callback_A;
    cbFns.StreamBCbFn = _rx_callback_B;
    cbFns.EventCbFn = _ev_callback;

#ifdef STREAMING_USB_MODE_BULK
    SoapySDR_log(SOAPY_SDR_INFO, "Using streaming USB mode bulk.");
    deviceParams->devParams->mode = sdrplay_api_BULK;
#endif

    {
        SdrplayApiLockGuard apiLock(SDRPLAY_API_TIMEOUT_MS);
        err = sdrplay_api_Init(device.dev, &cbFns, static_cast<void *>(this));
    }
    if (err != sdrplay_api_Success)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "error in activateStream() - Init() failed: %s", sdrplay_api_GetErrorString(err));
        // Clean up stream state that was set before Init() was called
        {
            std::lock_guard<std::mutex> streamsLock(_streams_mutex);
            _streamsRefCount[sdrplay_stream->channel]--;
            if (_streamsRefCount[sdrplay_stream->channel] == 0)
            {
                _streams[sdrplay_stream->channel] = nullptr;
            }
        }
        // Reset stream state so retry attempts start clean
        {
            std::lock_guard<std::mutex> streamLock(sdrplay_stream->mutex);
            sdrplay_stream->reset = false;
            sdrplay_stream->nElems = 0;
        }
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    streamActive = true;

    // Notify any threads waiting in readStream() that the stream is now active
    update_cv.notify_all();

    // Re-apply persistent antenna settings after stream activation
    // (some hardware may reset antenna during Init)
    bool shouldReapply[2] = {false, false};
    std::string antennaToApply[2];
    for (size_t ch = 0; ch < 2; ch++)
    {
        if (antennaPersistentEnabled[ch] && !persistentAntennaName[ch].empty())
        {
            shouldReapply[ch] = true;
            antennaToApply[ch] = persistentAntennaName[ch];
        }
    }

    // Initialize watchdog tracking for this stream
    {
        std::lock_guard<std::mutex> streamsLock(_streams_mutex);
        sdrplay_stream->lastCallbackTime = std::chrono::steady_clock::now();
        sdrplay_stream->lastWatchdogTicks = 0;
    }

    // Release lock before calling setAntenna (it takes the same lock)
    lock.unlock();

    for (size_t ch = 0; ch < 2; ch++)
    {
        if (shouldReapply[ch])
        {
            setAntenna(SOAPY_SDR_RX, ch, antennaToApply[ch]);
        }
    }

    // Start watchdog thread if enabled and not already running
    if (watchdogConfig.enabled && !watchdogRunning.load())
    {
        startWatchdog();
    }

    return 0;
}

int SoapySDRPlay::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs)
{
    if (flags != 0)
    {
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    // do nothing because deactivateStream() can be called multiple times
    return 0;
}

int SoapySDRPlay::readStream(SoapySDR::Stream *stream,
                             void * const *buffs,
                             const size_t numElems,
                             int &flags,
                             long long &timeNs,
                             const long timeoutUs)
{
    // Wait until either the timeout is reached or the stream is activated
    // Use condition variable instead of sleep for immediate wake-up when stream activates
    if (!streamActive)
    {
        std::unique_lock<std::mutex> lk(update_mutex);
        update_cv.wait_for(lk, std::chrono::microseconds(timeoutUs),
                           [this]{ return streamActive.load() || device_unavailable.load(); });
        if (!streamActive)
        {
            return SOAPY_SDR_TIMEOUT;
        }
    }

    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        if (_streams[sdrplay_stream->channel] == nullptr)
        {
            //throw std::runtime_error("readStream stream not activated");
            return SOAPY_SDR_NOT_SUPPORTED;
        }
    }

    // Serialize readStream operations on this stream
    std::lock_guard <std::mutex> lock(sdrplay_stream->readStreamMutex);

    // are elements left in the buffer? if not, do a new read.
    if (sdrplay_stream->nElems == 0)
    {
        int ret = this->acquireReadBuffer(stream, sdrplay_stream->currentHandle, (const void **)&sdrplay_stream->currentBuff, flags, timeNs, timeoutUs);

        if (ret < 0)
        {
            // Do not generate logs here, as interleaving with stream indicators
            //SoapySDR_logf(SOAPY_SDR_WARNING, "readStream() failed: %s", SoapySDR_errToStr(ret));
            return ret;
        }
        sdrplay_stream->nElems = ret;
    }

    size_t returnedElems = std::min(sdrplay_stream->nElems.load(), numElems);

    // Defensive null check for currentBuff
    if (sdrplay_stream->currentBuff == nullptr)
    {
        return SOAPY_SDR_STREAM_ERROR;
    }

    // copy into user's buff - always write to buffs[0] since each stream
    // can have only one rx/channel
    const size_t elemCount = returnedElems * elementsPerSample;
    if (useShort)
    {
        const auto *src = static_cast<const short *>(sdrplay_stream->currentBuff);
        std::memcpy(buffs[0], src, elemCount * sizeof(short));
    }
    else
    {
        const auto *src = static_cast<const float *>(sdrplay_stream->currentBuff);
        std::memcpy(buffs[0], src, elemCount * sizeof(float));
    }

    // bump variables for next call into readStream
    sdrplay_stream->nElems -= returnedElems;

    // scope lock here to update stream->currentBuff position
    {
        std::lock_guard <std::mutex> lock(sdrplay_stream->mutex);
        if (useShort)
        {
            auto *src = static_cast<short *>(sdrplay_stream->currentBuff);
            sdrplay_stream->currentBuff = src + elemCount;
        }
        else
        {
            auto *src = static_cast<float *>(sdrplay_stream->currentBuff);
            sdrplay_stream->currentBuff = src + elemCount;
        }
    }

    // return number of elements written to buff
    if (sdrplay_stream->nElems != 0)
    {
        flags |= SOAPY_SDR_MORE_FRAGMENTS;
    }
    else
    {
        this->releaseReadBuffer(stream, sdrplay_stream->currentHandle);
    }
    return static_cast<int>(returnedElems);
}

/*******************************************************************
 * Direct buffer access API
 ******************************************************************/

size_t SoapySDRPlay::getNumDirectAccessBuffers(SoapySDR::Stream *stream)
{
    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);
    if (sdrplay_stream == nullptr)
    {
        return 0;
    }
    std::lock_guard <std::mutex> lockA(sdrplay_stream->mutex);
    return useShort ? sdrplay_stream->shortBuffs.size() : sdrplay_stream->floatBuffs.size();
}

int SoapySDRPlay::getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **buffs)
{
    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);
    if (sdrplay_stream == nullptr)
    {
        return SOAPY_SDR_STREAM_ERROR;
    }
    std::lock_guard <std::mutex> lockA(sdrplay_stream->mutex);
    // validate handle is within bounds
    if (useShort)
    {
        if (handle >= sdrplay_stream->shortBuffs.size())
        {
            return SOAPY_SDR_OVERFLOW;
        }
        // always write to buffs[0] since each stream can have only one rx/channel
        buffs[0] = static_cast<void *>(sdrplay_stream->shortBuffs[handle].data());
    }
    else
    {
        if (handle >= sdrplay_stream->floatBuffs.size())
        {
            return SOAPY_SDR_OVERFLOW;
        }
        // always write to buffs[0] since each stream can have only one rx/channel
        buffs[0] = static_cast<void *>(sdrplay_stream->floatBuffs[handle].data());
    }
    return 0;
}

int SoapySDRPlay::acquireReadBuffer(SoapySDR::Stream *stream,
                                    size_t &handle,
                                    const void **buffs,
                                    int &flags,
                                    long long &timeNs,
                                    const long timeoutUs)
{
    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);
    if (sdrplay_stream == nullptr)
    {
        return SOAPY_SDR_STREAM_ERROR;
    }

    std::unique_lock <std::mutex> lock(sdrplay_stream->mutex);

    // reset is issued by various settings
    // overflow set in the rx callback thread
    if (sdrplay_stream->reset || sdrplay_stream->overflowEvent)
    {
        // drain all buffers from the fifo
        sdrplay_stream->tail = 0;
        sdrplay_stream->head = 0;
        sdrplay_stream->count = 0;
        if (useShort)
        {
            for (auto &buff : sdrplay_stream->shortBuffs) buff.clear();
        }
        else
        {
            for (auto &buff : sdrplay_stream->floatBuffs) buff.clear();
        }
        sdrplay_stream->overflowEvent = false;
        if (sdrplay_stream->reset)
        {
           sdrplay_stream->reset = false;
        }
        else
        {
           SoapySDR_log(SOAPY_SDR_SSI, "O");
           return SOAPY_SDR_OVERFLOW;
        }
    }

    // wait for a buffer to become available
    // Use predicate form to handle spurious wakeups correctly
    if (sdrplay_stream->count == 0)
    {
        // Track callback activity to detect if callbacks stop firing
        uint64_t ticksBefore = sdrplay_stream->lastCallbackTicks.load(std::memory_order_relaxed);

        bool hasData = sdrplay_stream->cond.wait_for(
            lock,
            std::chrono::microseconds(timeoutUs),
            [&sdrplay_stream, this]{ return sdrplay_stream->count > 0 || device_unavailable.load(); }
        );
        if (!hasData)
        {
            // Check if callbacks have stopped firing (stale callback detection)
            uint64_t ticksAfter = sdrplay_stream->lastCallbackTicks.load(std::memory_order_relaxed);
            if (ticksAfter == ticksBefore && streamActive.load())
            {
                SoapySDR_log(SOAPY_SDR_WARNING, "No callbacks received during timeout period - stream may be stale");
            }
            return SOAPY_SDR_TIMEOUT;
        }
    }

    if (device_unavailable)
    {
       SoapySDR_log(SOAPY_SDR_ERROR, "Device is unavailable");
       return SOAPY_SDR_NOT_SUPPORTED;
    }

    // extract handle and buffer
    handle = sdrplay_stream->head;
    // always write to buffs[0] since each stream can have only one rx/channel
    if (useShort)
    {
        buffs[0] = static_cast<void *>(sdrplay_stream->shortBuffs[handle].data());
    }
    else
    {
        buffs[0] = static_cast<void *>(sdrplay_stream->floatBuffs[handle].data());
    }
    flags = 0;

    // Use bitwise AND instead of modulo for power-of-2 numBuffers (faster)
    sdrplay_stream->head = (sdrplay_stream->head + 1) & (numBuffers - 1);

    // return number available
    if (useShort)
    {
        return static_cast<int>(sdrplay_stream->shortBuffs[handle].size() / elementsPerSample);
    }
    return static_cast<int>(sdrplay_stream->floatBuffs[handle].size() / elementsPerSample);
}

void SoapySDRPlay::releaseReadBuffer(SoapySDR::Stream *stream, const size_t handle)
{
    SoapySDRPlayStream *sdrplay_stream = reinterpret_cast<SoapySDRPlayStream *>(stream);
    if (sdrplay_stream == nullptr)
    {
        return;
    }
    std::lock_guard <std::mutex> lockA(sdrplay_stream->mutex);
    // validate handle is within bounds and count won't underflow
    if (sdrplay_stream->count == 0)
    {
        return;
    }
    if (useShort)
    {
        if (handle >= sdrplay_stream->shortBuffs.size())
        {
            return;
        }
        sdrplay_stream->shortBuffs[handle].clear();
    }
    else
    {
        if (handle >= sdrplay_stream->floatBuffs.size())
        {
            return;
        }
        sdrplay_stream->floatBuffs[handle].clear();
    }
    sdrplay_stream->count--;
}
