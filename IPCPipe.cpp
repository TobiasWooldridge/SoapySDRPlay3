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

#include "IPCPipe.hpp"
#include <SoapySDR/Logger.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <iomanip>

// IPCMessage implementation

void IPCMessage::setParam(const std::string& key, double value)
{
    std::ostringstream oss;
    oss << std::setprecision(17) << value;
    params[key] = oss.str();
}

void IPCMessage::setParam(const std::string& key, int64_t value)
{
    params[key] = std::to_string(value);
}

double IPCMessage::getParamDouble(const std::string& key, double defaultVal) const
{
    auto it = params.find(key);
    if (it == params.end()) return defaultVal;
    try {
        return std::stod(it->second);
    } catch (...) {
        return defaultVal;
    }
}

int64_t IPCMessage::getParamInt(const std::string& key, int64_t defaultVal) const
{
    auto it = params.find(key);
    if (it == params.end()) return defaultVal;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return defaultVal;
    }
}

std::vector<uint8_t> IPCMessage::serialize() const
{
    // Simple format: type(4) + numParams(4) + [keyLen(4) + key + valueLen(4) + value]...
    std::vector<uint8_t> data;

    // Reserve space for header
    data.resize(8);

    // Write type
    uint32_t typeVal = static_cast<uint32_t>(type);
    std::memcpy(&data[0], &typeVal, 4);

    // Write number of params
    uint32_t numParams = static_cast<uint32_t>(params.size());
    std::memcpy(&data[4], &numParams, 4);

    // Write params
    for (const auto& kv : params)
    {
        // Key length and key
        uint32_t keyLen = static_cast<uint32_t>(kv.first.size());
        size_t pos = data.size();
        data.resize(pos + 4 + keyLen);
        std::memcpy(&data[pos], &keyLen, 4);
        std::memcpy(&data[pos + 4], kv.first.data(), keyLen);

        // Value length and value
        uint32_t valueLen = static_cast<uint32_t>(kv.second.size());
        pos = data.size();
        data.resize(pos + 4 + valueLen);
        std::memcpy(&data[pos], &valueLen, 4);
        std::memcpy(&data[pos + 4], kv.second.data(), valueLen);
    }

    return data;
}

IPCMessage IPCMessage::deserialize(const std::vector<uint8_t>& data)
{
    IPCMessage msg;

    if (data.size() < 8)
    {
        return msg;
    }

    // Read type
    uint32_t typeVal;
    std::memcpy(&typeVal, &data[0], 4);
    msg.type = static_cast<IPCMessageType>(typeVal);

    // Read number of params
    uint32_t numParams;
    std::memcpy(&numParams, &data[4], 4);

    size_t pos = 8;
    for (uint32_t i = 0; i < numParams && pos < data.size(); ++i)
    {
        // Read key
        if (pos + 4 > data.size()) break;
        uint32_t keyLen;
        std::memcpy(&keyLen, &data[pos], 4);
        pos += 4;

        if (pos + keyLen > data.size()) break;
        std::string key(reinterpret_cast<const char*>(&data[pos]), keyLen);
        pos += keyLen;

        // Read value
        if (pos + 4 > data.size()) break;
        uint32_t valueLen;
        std::memcpy(&valueLen, &data[pos], 4);
        pos += 4;

        if (pos + valueLen > data.size()) break;
        std::string value(reinterpret_cast<const char*>(&data[pos]), valueLen);
        pos += valueLen;

        msg.params[key] = value;
    }

    return msg;
}

// IPCPipe implementation

bool IPCPipe::create(int& readFd, int& writeFd)
{
    int fds[2];
    if (pipe(fds) < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "IPCPipe: pipe() failed: %s", strerror(errno));
        return false;
    }

    // Set non-blocking mode
    for (int i = 0; i < 2; ++i)
    {
        int flags = fcntl(fds[i], F_GETFL, 0);
        fcntl(fds[i], F_SETFL, flags | O_NONBLOCK);
    }

    readFd = fds[0];
    writeFd = fds[1];
    return true;
}

IPCPipe::IPCPipe(int fd, bool ownsDescriptor)
    : fd_(fd)
    , ownsDescriptor_(ownsDescriptor)
{
}

IPCPipe::~IPCPipe()
{
    close();
}

void IPCPipe::close()
{
    if (fd_ >= 0 && ownsDescriptor_)
    {
        ::close(fd_);
    }
    fd_ = -1;
}

bool IPCPipe::hasData(unsigned int timeoutMs) const
{
    if (fd_ < 0) return false;

    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int result = poll(&pfd, 1, timeoutMs);
    return result > 0 && (pfd.revents & POLLIN);
}

bool IPCPipe::readExact(void* buffer, size_t count, unsigned int timeoutMs)
{
    if (fd_ < 0) return false;

    auto* ptr = static_cast<uint8_t*>(buffer);
    size_t remaining = count;

    while (remaining > 0)
    {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int result = poll(&pfd, 1, timeoutMs);
        if (result <= 0)
        {
            // Timeout or error
            return false;
        }

        ssize_t n = read(fd_, ptr, remaining);
        if (n <= 0)
        {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            return false;
        }

        ptr += n;
        remaining -= n;
    }

    return true;
}

bool IPCPipe::writeExact(const void* buffer, size_t count, unsigned int timeoutMs)
{
    if (fd_ < 0) return false;

    const auto* ptr = static_cast<const uint8_t*>(buffer);
    size_t remaining = count;

    while (remaining > 0)
    {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int result = poll(&pfd, 1, timeoutMs);
        if (result <= 0)
        {
            // Timeout or error
            return false;
        }

        ssize_t n = write(fd_, ptr, remaining);
        if (n <= 0)
        {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            return false;
        }

        ptr += n;
        remaining -= n;
    }

    return true;
}

bool IPCPipe::send(const IPCMessage& msg, unsigned int timeoutMs)
{
    std::vector<uint8_t> data = msg.serialize();

    // Send length first
    uint32_t len = static_cast<uint32_t>(data.size());
    if (!writeExact(&len, 4, timeoutMs))
    {
        return false;
    }

    // Send data
    return writeExact(data.data(), data.size(), timeoutMs);
}

bool IPCPipe::receive(IPCMessage& msg, unsigned int timeoutMs)
{
    // Read length
    uint32_t len;
    if (!readExact(&len, 4, timeoutMs))
    {
        return false;
    }

    // Sanity check
    if (len > 1024 * 1024)  // 1MB max
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "IPCPipe: Message too large: %u bytes", len);
        return false;
    }

    // Read data
    std::vector<uint8_t> data(len);
    if (!readExact(data.data(), len, timeoutMs))
    {
        return false;
    }

    msg = IPCMessage::deserialize(data);
    return true;
}

// IPCPipePair implementation

IPCPipePair* IPCPipePair::create()
{
    auto* pair = new IPCPipePair();

    // Create parent→child pipe
    int p2cRead, p2cWrite;
    if (!IPCPipe::create(p2cRead, p2cWrite))
    {
        delete pair;
        return nullptr;
    }

    // Create child→parent pipe
    int c2pRead, c2pWrite;
    if (!IPCPipe::create(c2pRead, c2pWrite))
    {
        ::close(p2cRead);
        ::close(p2cWrite);
        delete pair;
        return nullptr;
    }

    // Parent owns: p2cWrite (write to child), c2pRead (read from child)
    // Child owns: p2cRead (read from parent), c2pWrite (write to parent)

    pair->toChild_.reset(new IPCPipe(p2cWrite, true));
    pair->fromChild_.reset(new IPCPipe(c2pRead, true));
    pair->childRecv_.reset(new IPCPipe(p2cRead, false));  // Child will close
    pair->childSend_.reset(new IPCPipe(c2pWrite, false)); // Child will close

    pair->childRecvFd_ = p2cRead;
    pair->childSendFd_ = c2pWrite;

    return pair;
}

IPCPipePair::~IPCPipePair()
{
    // Unique_ptr will clean up
}

void IPCPipePair::closeParentSide()
{
    toChild_.reset();
    fromChild_.reset();
}

void IPCPipePair::closeChildSide()
{
    if (childRecvFd_ >= 0)
    {
        ::close(childRecvFd_);
        childRecvFd_ = -1;
    }
    if (childSendFd_ >= 0)
    {
        ::close(childSendFd_);
        childSendFd_ = -1;
    }
    childRecv_.reset();
    childSend_.reset();
}
