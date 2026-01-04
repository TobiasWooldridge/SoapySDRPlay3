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

#include <string>
#include <chrono>
#include <mutex>

// Cross-process file-based lock for serializing SDRplay API operations
// across multiple processes. Uses flock() on UNIX systems.
//
// Features:
// - Exclusive lock acquisition with optional timeout
// - Cooldown enforcement between operations (prevents API hammering)
// - RAII-style lock guard for exception safety
// - Reentrant within the same thread via local mutex
class SDRplayLock
{
public:
    // Default lock file location
    static constexpr const char* DEFAULT_LOCK_PATH = "/tmp/soapy_sdrplay.lock";

    // Default cooldown between operations (milliseconds)
    static constexpr double DEFAULT_COOLDOWN_MS = 2500.0;

    // Constructor - uses default or custom lock path
    explicit SDRplayLock(const std::string& lockPath = DEFAULT_LOCK_PATH);

    ~SDRplayLock();

    // Acquire exclusive lock with optional cooldown enforcement
    // Returns true on success, false on timeout
    // timeoutMs: max wait time (0 = infinite)
    // cooldownMs: minimum time between operations (0 = no cooldown)
    bool acquire(unsigned int timeoutMs = 0, double cooldownMs = DEFAULT_COOLDOWN_MS);

    // Release the lock and update timestamp for cooldown tracking
    void release();

    // Check if lock is currently held by this instance
    bool isHeld() const { return held_; }

    // Get the lock file path
    const std::string& path() const { return lockPath_; }

private:
    std::string lockPath_;
    int lockFd_;
    bool held_;
    std::mutex localMutex_;  // For thread-safety within same process

    // Get the last operation timestamp from lock file
    std::chrono::steady_clock::time_point getLastOperationTime();

    // Update the lock file modification time
    void updateTimestamp();
};

// RAII guard for SDRplayLock
class SDRplayLockGuard
{
public:
    // Acquire lock on construction
    // Throws std::runtime_error if acquisition times out
    explicit SDRplayLockGuard(SDRplayLock& lock,
                              unsigned int timeoutMs = 0,
                              double cooldownMs = SDRplayLock::DEFAULT_COOLDOWN_MS);

    // Release lock on destruction
    ~SDRplayLockGuard();

    // No copying
    SDRplayLockGuard(const SDRplayLockGuard&) = delete;
    SDRplayLockGuard& operator=(const SDRplayLockGuard&) = delete;

    // Move semantics
    SDRplayLockGuard(SDRplayLockGuard&& other) noexcept;
    SDRplayLockGuard& operator=(SDRplayLockGuard&& other) noexcept;

private:
    SDRplayLock* lock_;
    bool acquired_;
};
