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

#include "SoapySDRPlayWorker.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Formats.hpp>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <vector>

// Argument markers for worker mode
static const char* WORKER_MODE_ARG = "--sdrplay-worker";
static const char* WORKER_CMD_FD_ARG = "--cmd-fd";
static const char* WORKER_STATUS_FD_ARG = "--status-fd";
static const char* WORKER_SHM_ARG = "--shm-name";
static const char* WORKER_SERIAL_ARG = "--serial";

bool SoapySDRPlayWorker::isWorkerMode(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], WORKER_MODE_ARG) == 0)
        {
            return true;
        }
    }
    return false;
}

int SoapySDRPlayWorker::runAsWorker(int argc, char* argv[])
{
    int cmdFd = -1;
    int statusFd = -1;
    std::string shmName;
    std::string serial;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], WORKER_CMD_FD_ARG) == 0 && i + 1 < argc)
        {
            cmdFd = std::atoi(argv[++i]);
        }
        else if (strcmp(argv[i], WORKER_STATUS_FD_ARG) == 0 && i + 1 < argc)
        {
            statusFd = std::atoi(argv[++i]);
        }
        else if (strcmp(argv[i], WORKER_SHM_ARG) == 0 && i + 1 < argc)
        {
            shmName = argv[++i];
        }
        else if (strcmp(argv[i], WORKER_SERIAL_ARG) == 0 && i + 1 < argc)
        {
            serial = argv[++i];
        }
    }

    if (cmdFd < 0 || statusFd < 0 || shmName.empty() || serial.empty())
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "Worker: Invalid arguments");
        return 1;
    }

    SoapySDR::Kwargs args;
    args["driver"] = "sdrplay";  // Force direct driver lookup, skip enumeration
    args["serial"] = serial;

    return workerMain(cmdFd, statusFd, shmName, args);
}

int SoapySDRPlayWorker::workerMain(int cmdReadFd, int statusWriteFd,
                                   const std::string& shmName,
                                   const SoapySDR::Kwargs& deviceArgs)
{
    SoapySDRPlayWorker worker(cmdReadFd, statusWriteFd, shmName, deviceArgs);
    return worker.run();
}

SoapySDRPlayWorker::SoapySDRPlayWorker(int cmdReadFd, int statusWriteFd,
                                       const std::string& shmName,
                                       const SoapySDR::Kwargs& deviceArgs)
    : cmdPipe_(new IPCPipe(cmdReadFd, true))
    , statusPipe_(new IPCPipe(statusWriteFd, true))
    , deviceArgs_(deviceArgs)
{
    // Open existing shared memory (created by proxy)
    ringBuffer_.reset(SharedRingBuffer::open(shmName));
    if (!ringBuffer_)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "Worker: Failed to open shared memory %s", shmName.c_str());
    }
}

SoapySDRPlayWorker::~SoapySDRPlayWorker()
{
    running_ = false;

    if (streaming_ && streamThread_.joinable())
    {
        streaming_ = false;
        streamThread_.join();
    }

    if (stream_ && device_)
    {
        device_->deactivateStream(stream_);
        device_->closeStream(stream_);
        stream_ = nullptr;
    }

    device_.reset();
}

int SoapySDRPlayWorker::run()
{
    SoapySDR_logf(SOAPY_SDR_INFO, "Worker: Starting for device %s",
                 deviceArgs_["serial"].c_str());

    if (!ringBuffer_)
    {
        sendError("Failed to open shared memory");
        return 1;
    }

    // Send ready status
    sendStatus(IPCMessageType::STATUS_READY);
    running_ = true;

    // Main command loop
    while (running_)
    {
        IPCMessage cmd;
        if (!cmdPipe_->receive(cmd, 100))  // 100ms timeout for polling
        {
            continue;
        }

        switch (cmd.type)
        {
            case IPCMessageType::CMD_CONFIGURE:
                handleConfigure(cmd);
                break;

            case IPCMessageType::CMD_START:
                handleStart(cmd);
                break;

            case IPCMessageType::CMD_STOP:
                handleStop(cmd);
                break;

            case IPCMessageType::CMD_SHUTDOWN:
                running_ = false;
                sendAck();
                break;

            case IPCMessageType::CMD_SET_FREQUENCY:
                handleSetFrequency(cmd);
                break;

            case IPCMessageType::CMD_SET_SAMPLE_RATE:
                handleSetSampleRate(cmd);
                break;

            case IPCMessageType::CMD_SET_GAIN:
                handleSetGain(cmd);
                break;

            case IPCMessageType::CMD_SET_AGC:
                handleSetAgc(cmd);
                break;

            case IPCMessageType::CMD_SET_ANTENNA:
                handleSetAntenna(cmd);
                break;

            case IPCMessageType::CMD_SET_BANDWIDTH:
                handleSetBandwidth(cmd);
                break;

            case IPCMessageType::CMD_GET_STATUS:
                handleGetStatus(cmd);
                break;

            default:
                SoapySDR_logf(SOAPY_SDR_WARNING, "Worker: Unknown command type %d",
                             static_cast<int>(cmd.type));
                break;
        }
    }

    // Cleanup
    if (streaming_)
    {
        streaming_ = false;
        if (streamThread_.joinable())
        {
            streamThread_.join();
        }
    }

    ringBuffer_->setFlag(RINGBUF_FLAG_SHUTDOWN);
    SoapySDR_logf(SOAPY_SDR_INFO, "Worker: Exiting");
    return 0;
}

void SoapySDRPlayWorker::handleConfigure(const IPCMessage& cmd)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Worker: Handling configure");

    // Extract settings from message
    centerFreq_ = cmd.getParamDouble("center_hz", centerFreq_);
    sampleRate_ = cmd.getParamDouble("sample_rate", sampleRate_);
    bandwidth_ = cmd.getParamDouble("bandwidth", bandwidth_);
    gain_ = cmd.getParamDouble("gain", gain_);
    agcEnabled_ = cmd.getParamInt("agc", agcEnabled_ ? 1 : 0) != 0;
    antenna_ = cmd.getParam("antenna", antenna_);

    try
    {
        // Acquire cross-process lock
        SDRplayLockGuard lockGuard(lock_, 10000);  // 10s timeout

        // Open device if not already open
        if (!device_)
        {
            SoapySDR_logf(SOAPY_SDR_INFO, "Worker: Opening device %s",
                         deviceArgs_["serial"].c_str());

            device_.reset(SoapySDR::Device::make(deviceArgs_));
            if (!device_)
            {
                sendError("Failed to open device");
                return;
            }

            sendStatus(IPCMessageType::STATUS_OPENED);
        }

        // Apply settings
        if (!antenna_.empty())
        {
            device_->setAntenna(SOAPY_SDR_RX, 0, antenna_);
        }

        device_->setSampleRate(SOAPY_SDR_RX, 0, sampleRate_);
        device_->setFrequency(SOAPY_SDR_RX, 0, centerFreq_);

        if (bandwidth_ > 0)
        {
            device_->setBandwidth(SOAPY_SDR_RX, 0, bandwidth_);
        }

        device_->setGainMode(SOAPY_SDR_RX, 0, agcEnabled_);
        if (!agcEnabled_)
        {
            device_->setGain(SOAPY_SDR_RX, 0, gain_);
        }

        // Update ring buffer sample rate
        ringBuffer_->setSampleRate(static_cast<uint32_t>(sampleRate_));

        sendStatus(IPCMessageType::STATUS_CONFIGURED);
    }
    catch (const std::exception& e)
    {
        sendError(std::string("Configure failed: ") + e.what());
    }
}

void SoapySDRPlayWorker::handleStart(const IPCMessage& /* cmd */)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Worker: Handling start");

    if (!device_)
    {
        sendError("Device not configured");
        return;
    }

    if (streaming_)
    {
        sendAck();
        return;
    }

    try
    {
        // Acquire cross-process lock
        SDRplayLockGuard lockGuard(lock_, 10000);

        // Setup stream
        if (!stream_)
        {
            stream_ = device_->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32);
            if (!stream_)
            {
                sendError("Failed to setup stream");
                return;
            }
        }

        // Activate stream
        int ret = device_->activateStream(stream_);
        if (ret != 0)
        {
            sendError("Failed to activate stream: " + std::to_string(ret));
            return;
        }

        // Start streaming thread
        streaming_ = true;
        ringBuffer_->setFlag(RINGBUF_FLAG_RUNNING);
        streamThread_ = std::thread(&SoapySDRPlayWorker::streamingLoop, this);

        sendStatus(IPCMessageType::STATUS_STARTED);
    }
    catch (const std::exception& e)
    {
        sendError(std::string("Start failed: ") + e.what());
    }
}

void SoapySDRPlayWorker::handleStop(const IPCMessage& /* cmd */)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Worker: Handling stop");

    if (!streaming_)
    {
        sendAck();
        return;
    }

    streaming_ = false;
    ringBuffer_->clearFlag(RINGBUF_FLAG_RUNNING);

    if (streamThread_.joinable())
    {
        streamThread_.join();
    }

    if (device_ && stream_)
    {
        device_->deactivateStream(stream_);
    }

    sendStatus(IPCMessageType::STATUS_STOPPED);
}

void SoapySDRPlayWorker::handleSetFrequency(const IPCMessage& cmd)
{
    double freq = cmd.getParamDouble("value", centerFreq_);
    centerFreq_ = freq;

    if (device_)
    {
        try
        {
            device_->setFrequency(SOAPY_SDR_RX, 0, freq);
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set frequency failed: ") + e.what());
        }
    }
    else
    {
        sendAck();  // Will apply on configure
    }
}

void SoapySDRPlayWorker::handleSetSampleRate(const IPCMessage& cmd)
{
    double rate = cmd.getParamDouble("value", sampleRate_);
    sampleRate_ = rate;

    if (device_)
    {
        try
        {
            device_->setSampleRate(SOAPY_SDR_RX, 0, rate);
            ringBuffer_->setSampleRate(static_cast<uint32_t>(rate));
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set sample rate failed: ") + e.what());
        }
    }
    else
    {
        sendAck();
    }
}

void SoapySDRPlayWorker::handleSetGain(const IPCMessage& cmd)
{
    double gainVal = cmd.getParamDouble("value", gain_);
    gain_ = gainVal;

    if (device_)
    {
        try
        {
            device_->setGain(SOAPY_SDR_RX, 0, gainVal);
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set gain failed: ") + e.what());
        }
    }
    else
    {
        sendAck();
    }
}

void SoapySDRPlayWorker::handleSetAgc(const IPCMessage& cmd)
{
    bool enabled = cmd.getParamInt("value", agcEnabled_ ? 1 : 0) != 0;
    agcEnabled_ = enabled;

    if (device_)
    {
        try
        {
            device_->setGainMode(SOAPY_SDR_RX, 0, enabled);
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set AGC failed: ") + e.what());
        }
    }
    else
    {
        sendAck();
    }
}

void SoapySDRPlayWorker::handleSetAntenna(const IPCMessage& cmd)
{
    std::string ant = cmd.getParam("value", antenna_);
    antenna_ = ant;

    if (device_)
    {
        try
        {
            device_->setAntenna(SOAPY_SDR_RX, 0, ant);
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set antenna failed: ") + e.what());
        }
    }
    else
    {
        sendAck();
    }
}

void SoapySDRPlayWorker::handleSetBandwidth(const IPCMessage& cmd)
{
    double bw = cmd.getParamDouble("value", bandwidth_);
    bandwidth_ = bw;

    if (device_)
    {
        try
        {
            device_->setBandwidth(SOAPY_SDR_RX, 0, bw);
            sendAck();
        }
        catch (const std::exception& e)
        {
            sendError(std::string("Set bandwidth failed: ") + e.what());
        }
    }
    else
    {
        sendAck();
    }
}

void SoapySDRPlayWorker::handleGetStatus(const IPCMessage& /* cmd */)
{
    IPCMessage status(IPCMessageType::STATUS_STATS);
    status.setParam("streaming", streaming_ ? "true" : "false");
    status.setParam("center_hz", centerFreq_);
    status.setParam("sample_rate", sampleRate_);
    status.setParam("gain", gain_);
    status.setParam("agc", static_cast<int64_t>(agcEnabled_ ? 1 : 0));
    status.setParam("sample_count", static_cast<int64_t>(ringBuffer_->sampleCount()));
    status.setParam("overflow_count", static_cast<int64_t>(ringBuffer_->overflowCount()));
    statusPipe_->send(status);
}

void SoapySDRPlayWorker::sendStatus(IPCMessageType type, const std::string& message)
{
    IPCMessage status(type);
    if (!message.empty())
    {
        status.setParam("message", message);
    }
    statusPipe_->send(status);
}

void SoapySDRPlayWorker::sendError(const std::string& message)
{
    SoapySDR_logf(SOAPY_SDR_ERROR, "Worker: %s", message.c_str());
    IPCMessage status(IPCMessageType::STATUS_ERROR);
    status.setParam("message", message);
    ringBuffer_->setFlag(RINGBUF_FLAG_ERROR);
    statusPipe_->send(status);
}

void SoapySDRPlayWorker::sendAck()
{
    IPCMessage ack(IPCMessageType::STATUS_ACK);
    statusPipe_->send(ack);
}

void SoapySDRPlayWorker::streamingLoop()
{
    SoapySDR_logf(SOAPY_SDR_INFO, "Worker: Streaming loop started");

    constexpr size_t BUFFER_SIZE = 65536;
    std::vector<std::complex<float>> buffer(BUFFER_SIZE);
    void* buffs[] = { buffer.data() };

    while (streaming_)
    {
        int flags = 0;
        long long timeNs = 0;

        int ret = device_->readStream(stream_, buffs, BUFFER_SIZE, flags, timeNs, 100000);

        if (ret > 0)
        {
            size_t written = ringBuffer_->write(buffer.data(), ret);
            if (written < static_cast<size_t>(ret))
            {
                // Overflow
                IPCMessage overflow(IPCMessageType::STATUS_OVERFLOW);
                overflow.setParam("dropped", static_cast<int64_t>(ret - written));
                statusPipe_->send(overflow);
            }
        }
        else if (ret == SOAPY_SDR_TIMEOUT)
        {
            // Timeout is normal, continue
        }
        else if (ret < 0)
        {
            SoapySDR_logf(SOAPY_SDR_WARNING, "Worker: readStream error %d", ret);
        }
    }

    SoapySDR_logf(SOAPY_SDR_INFO, "Worker: Streaming loop ended");
}

// WorkerSpawner implementation

// Find the worker executable in standard locations
static std::string findWorkerExecutable()
{
    // Check common installation paths
    const char* searchPaths[] = {
        "/usr/local/lib/SoapySDR/modules0.8-3/sdrplay_worker",
        "/usr/lib/SoapySDR/modules0.8-3/sdrplay_worker",
        "/opt/homebrew/lib/SoapySDR/modules0.8/sdrplay_worker",
        nullptr
    };

    // Also check relative to home directory
    std::string homeWorker;
    const char* home = std::getenv("HOME");
    if (home)
    {
        homeWorker = std::string(home) + "/.local/lib/SoapySDR/modules0.8-3/sdrplay_worker";
    }

    // Check environment variable override
    const char* workerPath = std::getenv("SOAPY_SDRPLAY_WORKER");
    if (workerPath && access(workerPath, X_OK) == 0)
    {
        return workerPath;
    }

    // Check home directory first
    if (!homeWorker.empty() && access(homeWorker.c_str(), X_OK) == 0)
    {
        return homeWorker;
    }

    // Check standard paths
    for (const char** path = searchPaths; *path; ++path)
    {
        if (access(*path, X_OK) == 0)
        {
            return *path;
        }
    }

    return "";
}

pid_t WorkerSpawner::spawn(const SoapySDR::Kwargs& deviceArgs,
                           const std::string& shmName,
                           IPCPipePair** pipePairOut)
{
    // Create pipe pair
    IPCPipePair* pipes = IPCPipePair::create();
    if (!pipes)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: Failed to create pipes");
        return -1;
    }

    // Find worker executable
    std::string workerPath = findWorkerExecutable();
    if (workerPath.empty())
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: Cannot find sdrplay_worker executable. "
                     "Set SOAPY_SDRPLAY_WORKER environment variable or install to standard location.");
        delete pipes;
        return -1;
    }

    SoapySDR_logf(SOAPY_SDR_INFO, "WorkerSpawner: Using worker executable: %s", workerPath.c_str());

    pid_t pid = fork();

    if (pid < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: fork() failed: %s", strerror(errno));
        delete pipes;
        return -1;
    }

    if (pid == 0)
    {
        // Child process
        pipes->closeParentSide();

        // CRITICAL: Clear proxy mode env var so worker doesn't try to create another proxy
        unsetenv("SOAPY_SDRPLAY_MULTIDEV");

        // Build argument list
        std::string cmdFdStr = std::to_string(pipes->childReadFd());
        std::string statusFdStr = std::to_string(pipes->childWriteFd());

        std::string serial = deviceArgs.count("serial") ? deviceArgs.at("serial") : "";

        // Exec worker executable
        execl(workerPath.c_str(), workerPath.c_str(),
              WORKER_MODE_ARG,
              WORKER_CMD_FD_ARG, cmdFdStr.c_str(),
              WORKER_STATUS_FD_ARG, statusFdStr.c_str(),
              WORKER_SHM_ARG, shmName.c_str(),
              WORKER_SERIAL_ARG, serial.c_str(),
              nullptr);

        // If exec fails
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: exec failed: %s", strerror(errno));
        _exit(1);
    }

    // Parent process
    pipes->closeChildSide();
    *pipePairOut = pipes;

    SoapySDR_logf(SOAPY_SDR_INFO, "WorkerSpawner: Spawned worker PID %d for device %s",
                 pid, deviceArgs.count("serial") ? deviceArgs.at("serial").c_str() : "unknown");

    return pid;
}

bool WorkerSpawner::waitForReady(IPCPipe* statusPipe, unsigned int timeoutMs)
{
    IPCMessage msg;
    if (!statusPipe->receive(msg, timeoutMs))
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: Timeout waiting for worker ready");
        return false;
    }

    if (msg.type == IPCMessageType::STATUS_READY)
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "WorkerSpawner: Worker is ready");
        return true;
    }

    if (msg.type == IPCMessageType::STATUS_ERROR)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "WorkerSpawner: Worker reported error: %s",
                     msg.getParam("message").c_str());
        return false;
    }

    SoapySDR_logf(SOAPY_SDR_WARNING, "WorkerSpawner: Unexpected status %d",
                 static_cast<int>(msg.type));
    return false;
}

void WorkerSpawner::terminate(pid_t pid)
{
    if (pid <= 0) return;

    // Try graceful shutdown first
    kill(pid, SIGTERM);

    // Wait briefly
    int status;
    for (int i = 0; i < 10; ++i)
    {
        if (waitpid(pid, &status, WNOHANG) == pid)
        {
            SoapySDR_logf(SOAPY_SDR_DEBUG, "WorkerSpawner: Worker %d terminated", pid);
            return;
        }
        usleep(100000);  // 100ms
    }

    // Force kill
    SoapySDR_logf(SOAPY_SDR_WARNING, "WorkerSpawner: Force killing worker %d", pid);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}
