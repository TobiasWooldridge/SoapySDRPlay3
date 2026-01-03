/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2020 Franco Venturi

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

thread_local unsigned int SdrplayApiLockGuard::lockDepth = 0;

float SoapySDRPlay::sdrplay_api::ver = 0.0;

// Track whether API was successfully opened (for destructor)
static std::atomic<bool> g_apiOpened{false};

// Track whether API open has failed permanently (timeout or error)
static std::atomic<bool> g_apiOpenFailed{false};

// Singleton class for SDRplay API (only one per process)
SoapySDRPlay::sdrplay_api::sdrplay_api()
{
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: starting\n"); fflush(stderr);

    // If we already failed to open, don't try again
    if (g_apiOpenFailed) {
        fprintf(stderr, "[SDRplay] sdrplay_api ctor: previously failed, throwing\n"); fflush(stderr);
        throw std::runtime_error("SDRplay API previously failed to open - restart application to retry");
    }

    sdrplay_api_ErrT err;

    // Open API with timeout protection to prevent hangs/crashes when service is unresponsive
    // Use shared_ptr to future so we can detach a cleanup thread if timeout occurs
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: starting async Open\n"); fflush(stderr);
    auto openFuture = std::make_shared<std::future<sdrplay_api_ErrT>>(
        std::async(std::launch::async, []() {
            fprintf(stderr, "[SDRplay] sdrplay_api ctor: async Open thread starting\n"); fflush(stderr);
            auto result = sdrplay_api_Open();
            fprintf(stderr, "[SDRplay] sdrplay_api ctor: async Open returned %d\n", result); fflush(stderr);
            return result;
        })
    );

    fprintf(stderr, "[SDRplay] sdrplay_api ctor: waiting for Open with timeout\n"); fflush(stderr);
    auto status = openFuture->wait_for(std::chrono::milliseconds(SDRPLAY_API_TIMEOUT_MS));
    if (status == std::future_status::timeout) {
        fprintf(stderr, "[SDRplay] sdrplay_api ctor: Open timed out!\n"); fflush(stderr);
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_Open() timed out after %d ms", SDRPLAY_API_TIMEOUT_MS);
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "The SDRplay API service may be unresponsive. Try restarting it.");

        // Mark as permanently failed to prevent retry attempts
        g_apiOpenFailed = true;

        // Spawn cleanup thread to close API when Open eventually returns
        // This prevents blocking in the future destructor
        fprintf(stderr, "[SDRplay] sdrplay_api ctor: spawning cleanup thread\n"); fflush(stderr);
        std::thread([openFuture]() {
            try {
                fprintf(stderr, "[SDRplay] cleanup thread: waiting for Open\n"); fflush(stderr);
                auto result = openFuture->get();  // Wait for Open to complete
                fprintf(stderr, "[SDRplay] cleanup thread: Open returned %d\n", result); fflush(stderr);
                if (result == sdrplay_api_Success) {
                    fprintf(stderr, "[SDRplay] cleanup thread: closing API\n"); fflush(stderr);
                    sdrplay_api_Close();  // Clean up if it succeeded
                }
            } catch (...) {
                fprintf(stderr, "[SDRplay] cleanup thread: exception\n"); fflush(stderr);
            }
            fprintf(stderr, "[SDRplay] cleanup thread: exiting\n"); fflush(stderr);
        }).detach();

        fprintf(stderr, "[SDRplay] sdrplay_api ctor: throwing timeout exception\n"); fflush(stderr);
        throw std::runtime_error("sdrplay_api_Open() timed out - check SDRplay service");
    }

    fprintf(stderr, "[SDRplay] sdrplay_api ctor: wait_for returned, getting result\n"); fflush(stderr);
    err = openFuture->get();
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: get() returned %d\n", err); fflush(stderr);
    if (err != sdrplay_api_Success) {
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_Open() Error: %s", sdrplay_api_GetErrorString(err));
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "Please check the sdrplay_api service to make sure it is up. If it is up, please restart it.");
        g_apiOpenFailed = true;  // Mark as permanently failed
        throw std::runtime_error("sdrplay_api_Open() failed");
    }

    g_apiOpened = true;  // Mark API as successfully opened
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: Open succeeded, checking API version\n"); fflush(stderr);

    // Check API versions match - with timeout protection
    // Use shared_ptr to allow detaching cleanup thread on timeout
    auto localVerPtr = std::make_shared<float>(0.0f);
    auto versionFuture = std::make_shared<std::future<sdrplay_api_ErrT>>(
        std::async(std::launch::async, [localVerPtr]() {
            return sdrplay_api_ApiVersion(localVerPtr.get());
        })
    );

    auto versionStatus = versionFuture->wait_for(std::chrono::milliseconds(SDRPLAY_API_TIMEOUT_MS));
    if (versionStatus == std::future_status::timeout) {
        fprintf(stderr, "[SDRplay] sdrplay_api ctor: ApiVersion timed out\n"); fflush(stderr);
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_ApiVersion() timed out - service is unresponsive");
        g_apiOpenFailed = true;
        g_apiOpened = false;  // Don't try to close on destruction
        // Spawn cleanup thread so future destructor doesn't block
        std::thread([versionFuture, localVerPtr]() {
            try { versionFuture->get(); } catch (...) {}
        }).detach();
        throw std::runtime_error("sdrplay_api_ApiVersion() timed out");
    }

    err = versionFuture->get();
    ver = *localVerPtr;  // Copy to static member
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: ApiVersion returned %d, ver=%.3f\n", err, ver); fflush(stderr);
    if (err != sdrplay_api_Success) {
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "ApiVersion Error: %s", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        g_apiOpened = false;
        throw std::runtime_error("ApiVersion() failed");
    }
    if (ver != SDRPLAY_API_VERSION) {
        ::SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api version: '%.3f' does not equal build version: '%.3f'", ver, SDRPLAY_API_VERSION);
    }
    fprintf(stderr, "[SDRplay] sdrplay_api ctor: completed successfully\n"); fflush(stderr);
}

SoapySDRPlay::sdrplay_api::~sdrplay_api()
{
    // Only close if we successfully opened
    if (!g_apiOpened) {
        return;
    }

    // Close API with timeout protection to prevent hangs during shutdown
    auto closeFuture = std::make_shared<std::future<sdrplay_api_ErrT>>(
        std::async(std::launch::async, []() {
            return sdrplay_api_Close();
        })
    );

    auto status = closeFuture->wait_for(std::chrono::milliseconds(SDRPLAY_API_TIMEOUT_MS));
    if (status == std::future_status::timeout) {
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_Close() timed out - service may be unresponsive");
        // Detach cleanup thread so destructor can return
        std::thread([closeFuture]() {
            closeFuture->get();  // Wait for Close to complete
        }).detach();
        return;
    }

    sdrplay_api_ErrT err = closeFuture->get();
    if (err != sdrplay_api_Success) {
        ::SoapySDR_logf(SOAPY_SDR_ERROR, "sdrplay_api_Close() failed: %s", sdrplay_api_GetErrorString(err));
    }
}
