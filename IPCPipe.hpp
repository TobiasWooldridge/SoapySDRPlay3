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
#include <vector>
#include <map>
#include <memory>

// IPC message types for worker communication
enum class IPCMessageType : uint32_t
{
    // Control messages (proxy → worker)
    CMD_CONFIGURE = 1,
    CMD_START = 2,
    CMD_STOP = 3,
    CMD_SHUTDOWN = 4,
    CMD_SET_FREQUENCY = 5,
    CMD_SET_SAMPLE_RATE = 6,
    CMD_SET_GAIN = 7,
    CMD_SET_AGC = 8,
    CMD_SET_ANTENNA = 9,
    CMD_SET_BANDWIDTH = 10,
    CMD_GET_STATUS = 11,

    // Status messages (worker → proxy)
    STATUS_READY = 100,
    STATUS_OPENED = 101,
    STATUS_CONFIGURED = 102,
    STATUS_STARTED = 103,
    STATUS_STOPPED = 104,
    STATUS_ERROR = 105,
    STATUS_OVERFLOW = 106,
    STATUS_STATS = 107,
    STATUS_ACK = 108,
};

// IPC message structure - simple binary protocol
// Format: [type:4][payloadLen:4][payload:variable]
struct IPCMessage
{
    IPCMessageType type;
    std::map<std::string, std::string> params;  // Key-value parameters

    // Helper constructors
    IPCMessage() : type(IPCMessageType::STATUS_ACK) {}
    explicit IPCMessage(IPCMessageType t) : type(t) {}

    // Parameter accessors
    void setParam(const std::string& key, const std::string& value)
    {
        params[key] = value;
    }

    void setParam(const std::string& key, double value);
    void setParam(const std::string& key, int64_t value);

    std::string getParam(const std::string& key, const std::string& defaultVal = "") const
    {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : defaultVal;
    }

    double getParamDouble(const std::string& key, double defaultVal = 0.0) const;
    int64_t getParamInt(const std::string& key, int64_t defaultVal = 0) const;

    bool hasParam(const std::string& key) const
    {
        return params.find(key) != params.end();
    }

    // Serialization
    std::vector<uint8_t> serialize() const;
    static IPCMessage deserialize(const std::vector<uint8_t>& data);
};

// Pipe wrapper for IPC communication
// Non-blocking with timeout support
class IPCPipe
{
public:
    // Create a new pipe pair
    // Returns read/write file descriptors
    static bool create(int& readFd, int& writeFd);

    // Wrap existing file descriptor
    explicit IPCPipe(int fd, bool ownsDescriptor = false);

    ~IPCPipe();

    // Send a message (with optional timeout)
    // Returns true on success
    bool send(const IPCMessage& msg, unsigned int timeoutMs = 5000);

    // Receive a message (with optional timeout)
    // Returns true if message received, false on timeout/error
    bool receive(IPCMessage& msg, unsigned int timeoutMs = 5000);

    // Check if data is available for reading
    bool hasData(unsigned int timeoutMs = 0) const;

    // Close the pipe
    void close();

    // Get the file descriptor
    int fd() const { return fd_; }

    // Check if pipe is valid
    bool valid() const { return fd_ >= 0; }

private:
    int fd_;
    bool ownsDescriptor_;

    // Read exactly N bytes with timeout
    bool readExact(void* buffer, size_t count, unsigned int timeoutMs);

    // Write exactly N bytes with timeout
    bool writeExact(const void* buffer, size_t count, unsigned int timeoutMs);
};

// Pipe pair for bidirectional communication
class IPCPipePair
{
public:
    // Create a new pipe pair for bidirectional communication
    // Returns null on failure
    static IPCPipePair* create();

    ~IPCPipePair();

    // Parent side (proxy process)
    IPCPipe* parentToChild() { return toChild_.get(); }
    IPCPipe* childToParent() { return fromChild_.get(); }

    // Child side (worker process) - call after fork
    IPCPipe* childReceive() { return childRecv_.get(); }
    IPCPipe* childSend() { return childSend_.get(); }

    // Get file descriptors for passing to child via exec
    int childReadFd() const { return childRecvFd_; }
    int childWriteFd() const { return childSendFd_; }

    // Close parent-side descriptors (call in child after fork)
    void closeParentSide();

    // Close child-side descriptors (call in parent after fork)
    void closeChildSide();

private:
    IPCPipePair() = default;

    std::unique_ptr<IPCPipe> toChild_;    // Parent → Child (parent owns write, child owns read)
    std::unique_ptr<IPCPipe> fromChild_;  // Child → Parent (child owns write, parent owns read)
    std::unique_ptr<IPCPipe> childRecv_;  // Child's read end
    std::unique_ptr<IPCPipe> childSend_;  // Child's write end

    int childRecvFd_ = -1;
    int childSendFd_ = -1;
};
