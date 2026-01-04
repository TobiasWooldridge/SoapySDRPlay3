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

#include "SDRplayLock.hpp"
#include <SoapySDR/Logger.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <stdexcept>

SDRplayLock::SDRplayLock(const std::string& lockPath)
    : lockPath_(lockPath)
    , lockFd_(-1)
    , held_(false)
{
}

SDRplayLock::~SDRplayLock()
{
    if (held_)
    {
        release();
    }
    if (lockFd_ >= 0)
    {
        close(lockFd_);
    }
}

bool SDRplayLock::acquire(unsigned int timeoutMs, double cooldownMs)
{
    std::lock_guard<std::mutex> localGuard(localMutex_);

    // Already held by this instance
    if (held_)
    {
        return true;
    }

    // Open or create the lock file
    if (lockFd_ < 0)
    {
        lockFd_ = open(lockPath_.c_str(), O_CREAT | O_RDWR, 0666);
        if (lockFd_ < 0)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SDRplayLock: Failed to open lock file %s: %s",
                         lockPath_.c_str(), strerror(errno));
            return false;
        }
    }

    auto startTime = std::chrono::steady_clock::now();

    // Try to acquire exclusive lock
    while (true)
    {
        int result = flock(lockFd_, LOCK_EX | LOCK_NB);
        if (result == 0)
        {
            // Lock acquired
            break;
        }

        if (errno != EWOULDBLOCK)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SDRplayLock: flock failed: %s", strerror(errno));
            return false;
        }

        // Check timeout
        if (timeoutMs > 0)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs)
            {
                SoapySDR_logf(SOAPY_SDR_WARNING, "SDRplayLock: Timeout waiting for lock (%u ms)", timeoutMs);
                return false;
            }
        }

        // Brief sleep before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Enforce cooldown - wait if last operation was too recent
    if (cooldownMs > 0)
    {
        auto lastOpTime = getLastOperationTime();
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastOpTime).count();

        if (elapsed < cooldownMs)
        {
            auto waitMs = static_cast<int>(cooldownMs - elapsed);
            SoapySDR_logf(SOAPY_SDR_DEBUG, "SDRplayLock: Cooldown wait %d ms", waitMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        }
    }

    held_ = true;
    SoapySDR_logf(SOAPY_SDR_DEBUG, "SDRplayLock: Lock acquired");
    return true;
}

void SDRplayLock::release()
{
    std::lock_guard<std::mutex> localGuard(localMutex_);

    if (!held_)
    {
        return;
    }

    // Update timestamp for cooldown tracking
    updateTimestamp();

    // Release the lock
    if (lockFd_ >= 0)
    {
        flock(lockFd_, LOCK_UN);
    }

    held_ = false;
    SoapySDR_logf(SOAPY_SDR_DEBUG, "SDRplayLock: Lock released");
}

std::chrono::steady_clock::time_point SDRplayLock::getLastOperationTime()
{
    struct stat st;
    if (lockFd_ >= 0 && fstat(lockFd_, &st) == 0)
    {
        // Convert mtime to steady_clock approximation
        // Note: This is an approximation since mtime uses wall clock
        auto now = std::chrono::steady_clock::now();
        auto wallNow = std::chrono::system_clock::now();
        auto mtime = std::chrono::system_clock::from_time_t(st.st_mtime);

        auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(wallNow - mtime);
        return now - wallElapsed;
    }

    // If we can't stat, assume no cooldown needed
    return std::chrono::steady_clock::time_point::min();
}

void SDRplayLock::updateTimestamp()
{
    if (lockFd_ >= 0)
    {
        // Update file modification time to now
        // Using futimes with NULL sets both atime and mtime to current time
        futimes(lockFd_, nullptr);
    }
}

// SDRplayLockGuard implementation

SDRplayLockGuard::SDRplayLockGuard(SDRplayLock& lock, unsigned int timeoutMs, double cooldownMs)
    : lock_(&lock)
    , acquired_(false)
{
    if (!lock_->acquire(timeoutMs, cooldownMs))
    {
        throw std::runtime_error("SDRplayLockGuard: Failed to acquire lock");
    }
    acquired_ = true;
}

SDRplayLockGuard::~SDRplayLockGuard()
{
    if (acquired_ && lock_)
    {
        lock_->release();
    }
}

SDRplayLockGuard::SDRplayLockGuard(SDRplayLockGuard&& other) noexcept
    : lock_(other.lock_)
    , acquired_(other.acquired_)
{
    other.lock_ = nullptr;
    other.acquired_ = false;
}

SDRplayLockGuard& SDRplayLockGuard::operator=(SDRplayLockGuard&& other) noexcept
{
    if (this != &other)
    {
        if (acquired_ && lock_)
        {
            lock_->release();
        }
        lock_ = other.lock_;
        acquired_ = other.acquired_;
        other.lock_ = nullptr;
        other.acquired_ = false;
    }
    return *this;
}
