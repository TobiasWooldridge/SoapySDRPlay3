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

#include "IPCPipe.hpp"
#include "RingBuffer.hpp"
#include "SDRplayLock.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.h>

#include <string>
#include <memory>
#include <atomic>
#include <thread>

// Worker subprocess that owns the actual SDRplay device
// Runs in an isolated process, communicates with proxy via IPC
class SoapySDRPlayWorker
{
public:
    // Main entry point for worker subprocess
    // Called after fork/exec with file descriptors passed as arguments
    static int workerMain(int cmdReadFd, int statusWriteFd,
                          const std::string& shmName,
                          const SoapySDR::Kwargs& deviceArgs);

    // Check if current process should run as worker (checks argv)
    static bool isWorkerMode(int argc, char* argv[]);

    // Parse worker arguments and run
    static int runAsWorker(int argc, char* argv[]);

private:
    SoapySDRPlayWorker(int cmdReadFd, int statusWriteFd,
                       const std::string& shmName,
                       const SoapySDR::Kwargs& deviceArgs);
    ~SoapySDRPlayWorker();

    // Main event loop
    int run();

    // Command handlers
    void handleConfigure(const IPCMessage& cmd);
    void handleStart(const IPCMessage& cmd);
    void handleStop(const IPCMessage& cmd);
    void handleSetFrequency(const IPCMessage& cmd);
    void handleSetSampleRate(const IPCMessage& cmd);
    void handleSetGain(const IPCMessage& cmd);
    void handleSetAgc(const IPCMessage& cmd);
    void handleSetAntenna(const IPCMessage& cmd);
    void handleSetBandwidth(const IPCMessage& cmd);
    void handleGetStatus(const IPCMessage& cmd);

    // Send status/error messages
    void sendStatus(IPCMessageType type, const std::string& message = "");
    void sendError(const std::string& message);
    void sendAck();

    // Streaming thread
    void streamingLoop();

    // IPC communication
    std::unique_ptr<IPCPipe> cmdPipe_;
    std::unique_ptr<IPCPipe> statusPipe_;

    // Shared memory ring buffer
    std::unique_ptr<SharedRingBuffer> ringBuffer_;

    // Cross-process lock
    SDRplayLock lock_;

    // Device
    SoapySDR::Kwargs deviceArgs_;
    std::unique_ptr<SoapySDR::Device> device_;
    SoapySDR::Stream* stream_ = nullptr;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> streaming_{false};
    std::thread streamThread_;

    // Cached settings
    double centerFreq_ = 100e6;
    double sampleRate_ = 2e6;
    double bandwidth_ = 0;
    double gain_ = 40;
    bool agcEnabled_ = true;
    std::string antenna_;
};

// Worker process spawner utility
class WorkerSpawner
{
public:
    // Spawn a new worker subprocess
    // Returns PID on success, -1 on failure
    // Fills in the IPC pipe pair for communication
    static pid_t spawn(const SoapySDR::Kwargs& deviceArgs,
                       const std::string& shmName,
                       IPCPipePair** pipePairOut);

    // Wait for worker to become ready (receive STATUS_READY message)
    static bool waitForReady(IPCPipe* statusPipe, unsigned int timeoutMs = 10000);

    // Terminate a worker process
    static void terminate(pid_t pid);
};
