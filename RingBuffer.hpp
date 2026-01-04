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
#include <cstdint>
#include <cstddef>
#include <complex>
#include <atomic>

// Shared memory ring buffer for IQ sample transfer between processes
//
// Memory layout:
//   [Header: 64 bytes] [Ring buffer data: variable size]
//
// The header contains atomic counters for lock-free single-producer
// single-consumer operation. The ring buffer stores complex float samples.

// Flags for ring buffer state
constexpr uint32_t RINGBUF_FLAG_DATA_READY = 0x01;
constexpr uint32_t RINGBUF_FLAG_OVERFLOW   = 0x02;
constexpr uint32_t RINGBUF_FLAG_ERROR      = 0x04;
constexpr uint32_t RINGBUF_FLAG_RUNNING    = 0x08;
constexpr uint32_t RINGBUF_FLAG_SHUTDOWN   = 0x10;

// Ring buffer header stored at offset 0 in shared memory
// Uses atomic operations for lock-free SPSC access
// Note: Size may vary by platform due to atomic alignment requirements
struct RingBufferHeader
{
    std::atomic<uint64_t> writeIdx;       // Samples written by producer (worker)
    std::atomic<uint64_t> readIdx;        // Samples read by consumer (proxy) - informational
    std::atomic<uint64_t> sampleCount;    // Total samples transferred (stats)
    std::atomic<uint64_t> overflowCount;  // Ring buffer overrun count
    std::atomic<uint32_t> sampleRate;     // Current sample rate
    std::atomic<uint32_t> flags;          // State flags
    std::atomic<int64_t> timestampNs;     // Last write timestamp (nanoseconds)
};

// Default ring buffer size: 256MB = 32M complex float samples
constexpr size_t DEFAULT_RINGBUF_SAMPLES = 32 * 1024 * 1024;
constexpr size_t HEADER_SIZE = sizeof(RingBufferHeader);

class SharedRingBuffer
{
public:
    // Create a new shared memory ring buffer (producer side)
    // name: shared memory name (e.g., "/sdrplay_12345")
    // numSamples: number of complex float samples to hold
    static SharedRingBuffer* create(const std::string& name,
                                    size_t numSamples = DEFAULT_RINGBUF_SAMPLES);

    // Open an existing shared memory ring buffer (consumer side)
    // name: shared memory name
    static SharedRingBuffer* open(const std::string& name);

    ~SharedRingBuffer();

    // Producer API (worker process)

    // Write samples to the ring buffer
    // Returns number of samples actually written (may be less if buffer full)
    size_t write(const std::complex<float>* samples, size_t count);

    // Write samples with conversion from CS16 format
    size_t writeCS16(const int16_t* samples, size_t count);

    // Set current sample rate
    void setSampleRate(uint32_t rate);

    // Set state flags
    void setFlag(uint32_t flag);
    void clearFlag(uint32_t flag);

    // Increment overflow counter
    void recordOverflow();

    // Consumer API (proxy process)

    // Read samples from the ring buffer
    // Returns number of samples actually read
    // timeoutUs: max wait time in microseconds (0 = non-blocking)
    size_t read(std::complex<float>* samples, size_t maxCount, long timeoutUs = 0);

    // Get number of samples available for reading
    size_t available() const;

    // Get current read position (for zero-copy access)
    const std::complex<float>* getReadPtr(size_t* availableOut) const;

    // Advance read position after zero-copy read
    void advanceRead(size_t count);

    // Common API

    // Get header (for status inspection)
    const RingBufferHeader* header() const { return header_; }

    // Get current flags
    uint32_t flags() const { return header_->flags.load(std::memory_order_acquire); }

    // Get overflow count
    uint64_t overflowCount() const { return header_->overflowCount.load(std::memory_order_acquire); }

    // Get total samples transferred
    uint64_t sampleCount() const { return header_->sampleCount.load(std::memory_order_acquire); }

    // Get sample rate
    uint32_t sampleRate() const { return header_->sampleRate.load(std::memory_order_acquire); }

    // Get current write index (for health monitoring)
    uint64_t writeIndex() const { return header_->writeIdx.load(std::memory_order_acquire); }

    // Get buffer capacity in samples
    size_t capacity() const { return numSamples_; }

    // Get shared memory name
    const std::string& name() const { return name_; }

    // Re-attach to shared memory (for stale mapping recovery)
    bool reattach();

private:
    SharedRingBuffer(const std::string& name, void* mapping, size_t mappingSize,
                     size_t numSamples, bool owner);

    std::string name_;
    void* mapping_;
    size_t mappingSize_;
    size_t numSamples_;
    bool owner_;  // True if we created the shared memory (responsible for cleanup)

    RingBufferHeader* header_;
    std::complex<float>* data_;

    // Last known read position (consumer only)
    uint64_t lastReadIdx_;
    uint64_t lastOverflowCount_;
};

// Utility function to generate unique shared memory name
std::string generateShmName(const std::string& deviceSerial);
