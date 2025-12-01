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
    return self->rx_callback(xi, xq, params, numSamples, self->_streams[0]);
}

static void _rx_callback_B(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params,
                           unsigned int numSamples, unsigned int reset, void *cbContext)
{
    auto *self = static_cast<SoapySDRPlay *>(cbContext);
    return self->rx_callback(xi, xq, params, numSamples, self->_streams[1]);
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
    if (stream == nullptr || device_unavailable) {
        return;
    }
    std::lock_guard<std::mutex> lock(stream->mutex);

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

    int spaceReqd = numSamples * elementsPerSample * shortsPerWord;
    unsigned int decFactor = chParams->ctrlParams.decimation.decimationFactor;
    if (decFactor == 0) decFactor = 1;  // Prevent division by zero
    if ((stream->buffs[stream->tail].size() + spaceReqd) >= (bufferLength / decFactor))
    {
       // increment the tail pointer and buffer count
       stream->tail = (stream->tail + 1) % numBuffers;
       stream->count++;

       auto &buff = stream->buffs[stream->tail];
       if (stream->count == numBuffers && static_cast<size_t>(spaceReqd) > buff.capacity() - buff.size())
       {
           stream->overflowEvent = true;
           return;
       }

       // notify readStream()
       stream->cond.notify_one();
    }

    // get current fill buffer
    auto &buff = stream->buffs[stream->tail];

    // Check if resize would exceed capacity (would cause reallocation)
    size_t newSize = buff.size() + spaceReqd;
    if (newSize > buff.capacity())
    {
        stream->overflowEvent = true;
        return;
    }

    // resize within pre-allocated capacity (no reallocation)
    buff.resize(newSize);

    // copy into the buffer queue
    unsigned int i = 0;

    if (useShort)
    {
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
       // Use multiplication by reciprocal instead of division for performance
       // (multiplication is faster than division in the hot path)
       constexpr float SCALE = 1.0f / 32768.0f;
       auto *dptr = reinterpret_cast<float *>(buff.data());
       dptr += ((buff.size() - spaceReqd) / shortsPerWord);
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
            sdrplay_api_ErrT err = sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
            if (err != sdrplay_api_Success)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api_Update(Ctrl_OverloadMsgAck) failed: %s", sdrplay_api_GetErrorString(err));
            }
            // OVERLOAD DETECTED
        }
        else if (powerOverloadChangeType == sdrplay_api_Overload_Corrected)
        {
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
            std::lock_guard<std::mutex> lock(_general_state_mutex);
            if (_streams[0]) _streams[0]->cond.notify_all();
            if (_streams[1]) _streams[1]->cond.notify_all();
        }
    }
    else if (eventId == sdrplay_api_RspDuoModeChange)
    {
        if (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterDllDisappeared)
        {
            // Notify readStream() that the master stream has been removed
            // so that the application can be closed gracefully
            SoapySDR_log(SOAPY_SDR_ERROR, "Master stream has been removed. Stopping.");
            device_unavailable = true;
            // Wake up any waiting threads so they can exit gracefully
            update_cv.notify_all();
            // Safely access stream pointers under lock to prevent use-after-free
            {
                std::lock_guard<std::mutex> lock(_general_state_mutex);
                if (_streams[0]) _streams[0]->cond.notify_all();
                if (_streams[1]) _streams[1]->cond.notify_all();
            }
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
    buffs.resize(numBuffers);
    for (auto &buff : buffs) buff.reserve(bufferLength);
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
        shortsPerWord = 1;
        bufferLength = bufferElems * elementsPerSample * shortsPerWord;
        SoapySDR_log(SOAPY_SDR_INFO, "Using format CS16.");
    }
    else if (format == "CF32")
    {
        useShort = false;
        shortsPerWord = sizeof(float) / sizeof(short);
        bufferLength = bufferElems * elementsPerSample * shortsPerWord;  // allocate enough space for floats instead of shorts
        SoapySDR_log(SOAPY_SDR_INFO, "Using format CF32.");
    }
    else
    {
        throw std::runtime_error( "setupStream invalid format '" + format +
                                  "' -- Only CS16 or CF32 are supported by the SoapySDRPlay module.");
    }

    // default is channel 0
    size_t channel = channels.size() == 0 ? 0 : channels.at(0);
    SoapySDRPlayStream *sdrplay_stream;
    {
        std::lock_guard<std::mutex> lock(_general_state_mutex);
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
        // We must acquire the stream's mutex to safely notify and then wait for
        // any threads in readStream/acquireReadBuffer to exit before deleting.
        {
            std::lock_guard<std::mutex> streamLock(sdrplay_stream->mutex);
            sdrplay_stream->cond.notify_all();
        }
        // Acquire readStreamMutex to ensure any thread in readStream() has exited.
        // This is safe because we've already set _streams[i] = nullptr above,
        // so new readStream() calls will return early, and existing ones will
        // exit after seeing the stream is gone or timing out.
        {
            std::lock_guard<std::mutex> readLock(sdrplay_stream->readStreamMutex);
        }
        delete sdrplay_stream;
    }
    if (activeStreams == 0)
    {
        while (true)
        {
            sdrplay_api_ErrT err;
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

    std::lock_guard <std::mutex> lock(_general_state_mutex);

    sdrplay_stream->reset = true;
    sdrplay_stream->nElems = 0;
    _streams[sdrplay_stream->channel] = sdrplay_stream;
    _streamsRefCount[sdrplay_stream->channel]++;

    if (streamActive)
    {
        return 0;
    }

    // Enable (= sdrplay_api_DbgLvl_Verbose) API calls tracing,
    // but only for debug purposes due to its performance impact.
    sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Disable);
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

    err = sdrplay_api_Init(device.dev, &cbFns, static_cast<void *>(this));
    if (err != sdrplay_api_Success)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "error in activateStream() - Init() failed: %s", sdrplay_api_GetErrorString(err));
        // Clean up stream state that was set before Init() was called
        _streamsRefCount[sdrplay_stream->channel]--;
        if (_streamsRefCount[sdrplay_stream->channel] == 0)
        {
            _streams[sdrplay_stream->channel] = nullptr;
        }
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    streamActive = true;

    // Notify any threads waiting in readStream() that the stream is now active
    update_cv.notify_all();

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
        std::lock_guard<std::mutex> lock(_general_state_mutex);
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
    if (useShort)
    {
        std::memcpy(buffs[0], sdrplay_stream->currentBuff, returnedElems * 2 * sizeof(short));
    }
    else
    {
        std::memcpy(buffs[0], reinterpret_cast<const float *>(sdrplay_stream->currentBuff), returnedElems * 2 * sizeof(float));
    }

    // bump variables for next call into readStream
    sdrplay_stream->nElems -= returnedElems;

    // scope lock here to update stream->currentBuff position
    {
        std::lock_guard <std::mutex> lock(sdrplay_stream->mutex);
        sdrplay_stream->currentBuff += returnedElems * elementsPerSample * shortsPerWord;
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
    return sdrplay_stream->buffs.size();
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
    if (handle >= sdrplay_stream->buffs.size())
    {
        return SOAPY_SDR_OVERFLOW;
    }
    // always write to buffs[0] since each stream can have only one rx/channel
    buffs[0] = static_cast<void *>(sdrplay_stream->buffs[handle].data());
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
        for (auto &buff : sdrplay_stream->buffs) buff.clear();
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
    if (sdrplay_stream->count == 0)
    {
        sdrplay_stream->cond.wait_for(lock, std::chrono::microseconds(timeoutUs));
        if (sdrplay_stream->count == 0)
        {
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
    buffs[0] = static_cast<void *>(sdrplay_stream->buffs[handle].data());
    flags = 0;

    sdrplay_stream->head = (sdrplay_stream->head + 1) % numBuffers;

    // return number available
    return static_cast<int>(sdrplay_stream->buffs[handle].size() / (elementsPerSample * shortsPerWord));
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
    if (handle >= sdrplay_stream->buffs.size() || sdrplay_stream->count == 0)
    {
        return;
    }
    sdrplay_stream->buffs[handle].clear();
    sdrplay_stream->count--;
}
