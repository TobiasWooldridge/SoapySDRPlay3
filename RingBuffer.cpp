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

#include "RingBuffer.hpp"
#include <SoapySDR/Logger.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>
#include <sstream>

SharedRingBuffer::SharedRingBuffer(const std::string& name, void* mapping, size_t mappingSize,
                                   size_t numSamples, bool owner)
    : name_(name)
    , mapping_(mapping)
    , mappingSize_(mappingSize)
    , numSamples_(numSamples)
    , owner_(owner)
    , lastReadIdx_(0)
    , lastOverflowCount_(0)
{
    header_ = static_cast<RingBufferHeader*>(mapping_);
    data_ = reinterpret_cast<std::complex<float>*>(
        static_cast<char*>(mapping_) + HEADER_SIZE);
}

SharedRingBuffer* SharedRingBuffer::create(const std::string& name, size_t numSamples)
{
    size_t dataSize = numSamples * sizeof(std::complex<float>);
    size_t totalSize = HEADER_SIZE + dataSize;

    // Unlink any existing shared memory with this name
    shm_unlink(name.c_str());

    // Create new shared memory
    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: shm_open create failed for %s: %s",
                     name.c_str(), strerror(errno));
        return nullptr;
    }

    // Set size
    if (ftruncate(fd, totalSize) < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: ftruncate failed: %s", strerror(errno));
        close(fd);
        shm_unlink(name.c_str());
        return nullptr;
    }

    // Map into memory
    void* mapping = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (mapping == MAP_FAILED)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: mmap failed: %s", strerror(errno));
        shm_unlink(name.c_str());
        return nullptr;
    }

    // Initialize header
    auto* header = static_cast<RingBufferHeader*>(mapping);
    new (&header->writeIdx) std::atomic<uint64_t>(0);
    new (&header->readIdx) std::atomic<uint64_t>(0);
    new (&header->sampleCount) std::atomic<uint64_t>(0);
    new (&header->overflowCount) std::atomic<uint64_t>(0);
    new (&header->sampleRate) std::atomic<uint32_t>(0);
    new (&header->flags) std::atomic<uint32_t>(0);
    new (&header->timestampNs) std::atomic<int64_t>(0);

    SoapySDR_logf(SOAPY_SDR_INFO, "SharedRingBuffer: Created %s with %zu samples (%.1f MB)",
                 name.c_str(), numSamples, totalSize / (1024.0 * 1024.0));

    return new SharedRingBuffer(name, mapping, totalSize, numSamples, true);
}

SharedRingBuffer* SharedRingBuffer::open(const std::string& name)
{
    // Open existing shared memory
    int fd = shm_open(name.c_str(), O_RDWR, 0666);
    if (fd < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: shm_open failed for %s: %s",
                     name.c_str(), strerror(errno));
        return nullptr;
    }

    // Get size
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: fstat failed: %s", strerror(errno));
        close(fd);
        return nullptr;
    }

    size_t totalSize = st.st_size;
    if (totalSize < HEADER_SIZE)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: Invalid size %zu", totalSize);
        close(fd);
        return nullptr;
    }

    // Map into memory
    void* mapping = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (mapping == MAP_FAILED)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: mmap failed: %s", strerror(errno));
        return nullptr;
    }

    size_t numSamples = (totalSize - HEADER_SIZE) / sizeof(std::complex<float>);

    SoapySDR_logf(SOAPY_SDR_INFO, "SharedRingBuffer: Opened %s with %zu samples",
                 name.c_str(), numSamples);

    return new SharedRingBuffer(name, mapping, totalSize, numSamples, false);
}

SharedRingBuffer::~SharedRingBuffer()
{
    if (mapping_ != nullptr && mapping_ != MAP_FAILED)
    {
        munmap(mapping_, mappingSize_);
    }

    if (owner_)
    {
        shm_unlink(name_.c_str());
        SoapySDR_logf(SOAPY_SDR_INFO, "SharedRingBuffer: Cleaned up %s", name_.c_str());
    }
}

bool SharedRingBuffer::reattach()
{
    // Unmap current mapping
    if (mapping_ != nullptr && mapping_ != MAP_FAILED)
    {
        munmap(mapping_, mappingSize_);
        mapping_ = nullptr;
    }

    // Reopen shared memory
    int fd = shm_open(name_.c_str(), O_RDWR, 0666);
    if (fd < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: reattach shm_open failed: %s",
                     strerror(errno));
        return false;
    }

    // Map into memory
    void* mapping = mmap(nullptr, mappingSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (mapping == MAP_FAILED)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "SharedRingBuffer: reattach mmap failed: %s",
                     strerror(errno));
        return false;
    }

    mapping_ = mapping;
    header_ = static_cast<RingBufferHeader*>(mapping_);
    data_ = reinterpret_cast<std::complex<float>*>(
        static_cast<char*>(mapping_) + HEADER_SIZE);

    SoapySDR_logf(SOAPY_SDR_DEBUG, "SharedRingBuffer: Reattached to %s", name_.c_str());
    return true;
}

// Producer API

size_t SharedRingBuffer::write(const std::complex<float>* samples, size_t count)
{
    uint64_t writeIdx = header_->writeIdx.load(std::memory_order_relaxed);
    uint64_t readIdx = header_->readIdx.load(std::memory_order_acquire);

    // Calculate available space
    uint64_t used = writeIdx - readIdx;
    size_t available = numSamples_ - used;

    if (count > available)
    {
        // Would overflow - record and truncate
        recordOverflow();
        count = available;
    }

    if (count == 0)
    {
        return 0;
    }

    // Write to ring buffer (may wrap around)
    size_t writePos = writeIdx % numSamples_;
    size_t firstChunk = std::min(count, numSamples_ - writePos);

    std::memcpy(&data_[writePos], samples, firstChunk * sizeof(std::complex<float>));

    if (count > firstChunk)
    {
        // Wrap around
        size_t secondChunk = count - firstChunk;
        std::memcpy(data_, samples + firstChunk, secondChunk * sizeof(std::complex<float>));
    }

    // Update write index (release semantics for visibility to consumer)
    header_->writeIdx.store(writeIdx + count, std::memory_order_release);
    header_->sampleCount.fetch_add(count, std::memory_order_relaxed);

    // Update timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t timestampNs = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    header_->timestampNs.store(timestampNs, std::memory_order_relaxed);

    return count;
}

size_t SharedRingBuffer::writeCS16(const int16_t* samples, size_t count)
{
    // Convert CS16 to CF32 in chunks
    constexpr size_t CHUNK_SIZE = 4096;
    std::complex<float> buffer[CHUNK_SIZE];
    size_t totalWritten = 0;

    const float scale = 1.0f / 32768.0f;

    while (count > 0)
    {
        size_t chunk = std::min(count, CHUNK_SIZE);

        for (size_t i = 0; i < chunk; ++i)
        {
            buffer[i] = std::complex<float>(
                samples[i * 2] * scale,
                samples[i * 2 + 1] * scale
            );
        }

        size_t written = write(buffer, chunk);
        totalWritten += written;

        if (written < chunk)
        {
            break;  // Buffer full
        }

        samples += chunk * 2;
        count -= chunk;
    }

    return totalWritten;
}

void SharedRingBuffer::setSampleRate(uint32_t rate)
{
    header_->sampleRate.store(rate, std::memory_order_release);
}

void SharedRingBuffer::setFlag(uint32_t flag)
{
    header_->flags.fetch_or(flag, std::memory_order_release);
}

void SharedRingBuffer::clearFlag(uint32_t flag)
{
    header_->flags.fetch_and(~flag, std::memory_order_release);
}

void SharedRingBuffer::recordOverflow()
{
    header_->overflowCount.fetch_add(1, std::memory_order_relaxed);
    setFlag(RINGBUF_FLAG_OVERFLOW);
}

// Consumer API

size_t SharedRingBuffer::read(std::complex<float>* samples, size_t maxCount, long timeoutUs)
{
    auto startTime = std::chrono::steady_clock::now();

    while (true)
    {
        uint64_t writeIdx = header_->writeIdx.load(std::memory_order_acquire);
        uint64_t readIdx = lastReadIdx_;

        size_t avail = writeIdx - readIdx;

        if (avail > 0)
        {
            size_t count = std::min(avail, maxCount);
            size_t readPos = readIdx % numSamples_;
            size_t firstChunk = std::min(count, numSamples_ - readPos);

            std::memcpy(samples, &data_[readPos], firstChunk * sizeof(std::complex<float>));

            if (count > firstChunk)
            {
                size_t secondChunk = count - firstChunk;
                std::memcpy(samples + firstChunk, data_, secondChunk * sizeof(std::complex<float>));
            }

            lastReadIdx_ = readIdx + count;
            header_->readIdx.store(lastReadIdx_, std::memory_order_release);

            // Check for overflow since last read
            uint64_t currentOverflow = header_->overflowCount.load(std::memory_order_relaxed);
            if (currentOverflow > lastOverflowCount_)
            {
                lastOverflowCount_ = currentOverflow;
                // Overflow occurred - consumer may have missed samples
            }

            return count;
        }

        // No data available
        if (timeoutUs <= 0)
        {
            return 0;
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed >= timeoutUs)
        {
            return 0;
        }

        // Brief sleep before retry
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

size_t SharedRingBuffer::available() const
{
    uint64_t writeIdx = header_->writeIdx.load(std::memory_order_acquire);
    return writeIdx - lastReadIdx_;
}

const std::complex<float>* SharedRingBuffer::getReadPtr(size_t* availableOut) const
{
    uint64_t writeIdx = header_->writeIdx.load(std::memory_order_acquire);
    size_t avail = writeIdx - lastReadIdx_;

    if (avail == 0)
    {
        *availableOut = 0;
        return nullptr;
    }

    size_t readPos = lastReadIdx_ % numSamples_;

    // Return contiguous chunk (up to end of buffer)
    *availableOut = std::min(avail, numSamples_ - readPos);
    return &data_[readPos];
}

void SharedRingBuffer::advanceRead(size_t count)
{
    lastReadIdx_ += count;
    header_->readIdx.store(lastReadIdx_, std::memory_order_release);
}

// Utility function

std::string generateShmName(const std::string& deviceSerial)
{
    std::ostringstream oss;
    oss << "/sdrplay_" << deviceSerial << "_" << getpid();
    return oss.str();
}
