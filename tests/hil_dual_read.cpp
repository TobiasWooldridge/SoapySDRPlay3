#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

struct StreamConfig
{
    std::string serial;
    std::string format = "CS16";
    double rate = 2000000.0;
    double freq = 100000000.0;
    double durationSec = 10.0;
    size_t numElems = 4096;
    long timeoutUs = 100000;
    int maxTimeouts = 20;
};

struct StreamStats
{
    size_t reads = 0;
    size_t samples = 0;
    size_t timeouts = 0;
    size_t errors = 0;
    int lastError = 0;
};

static bool parseArgValue(int &i, int argc, char **argv, std::string &value)
{
    if (i + 1 >= argc)
    {
        return false;
    }
    value = argv[++i];
    return true;
}

static StreamStats run_stream(const StreamConfig &cfg)
{
    StreamStats stats{};
    SoapySDR::Kwargs args;
    args["driver"] = "sdrplay";
    args["serial"] = cfg.serial;

    SoapySDR::Device *device = SoapySDR::Device::make(args);
    if (device == nullptr)
    {
        stats.errors = 1;
        stats.lastError = SOAPY_SDR_NOT_SUPPORTED;
        return stats;
    }

    device->setSampleRate(SOAPY_SDR_RX, 0, cfg.rate);
    device->setFrequency(SOAPY_SDR_RX, 0, cfg.freq);

    SoapySDR::Stream *stream = device->setupStream(SOAPY_SDR_RX, cfg.format);
    device->activateStream(stream);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(cfg.durationSec);
    int consecutiveTimeouts = 0;

    while (std::chrono::steady_clock::now() < deadline)
    {
        int flags = 0;
        long long timeNs = 0;
        int ret = 0;

        if (cfg.format == "CS16")
        {
            std::vector<short> buffer(cfg.numElems * 2);
            void *buffs[] = { buffer.data() };
            ret = device->readStream(stream, buffs, cfg.numElems, flags, timeNs, cfg.timeoutUs);
        }
        else
        {
            std::vector<float> buffer(cfg.numElems * 2);
            void *buffs[] = { buffer.data() };
            ret = device->readStream(stream, buffs, cfg.numElems, flags, timeNs, cfg.timeoutUs);
        }

        if (ret == SOAPY_SDR_TIMEOUT)
        {
            stats.timeouts++;
            consecutiveTimeouts++;
            if (consecutiveTimeouts > cfg.maxTimeouts)
            {
                stats.errors++;
                stats.lastError = SOAPY_SDR_TIMEOUT;
                break;
            }
            continue;
        }

        if (ret < 0)
        {
            stats.errors++;
            stats.lastError = ret;
            break;
        }

        consecutiveTimeouts = 0;
        stats.reads++;
        stats.samples += static_cast<size_t>(ret);
    }

    device->deactivateStream(stream);
    device->closeStream(stream);
    SoapySDR::Device::unmake(device);

    return stats;
}

int main(int argc, char **argv)
{
    StreamConfig cfgA;
    StreamConfig cfgB;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        std::string value;

        if (arg == "--serial-a" && parseArgValue(i, argc, argv, value))
        {
            cfgA.serial = value;
        }
        else if (arg == "--serial-b" && parseArgValue(i, argc, argv, value))
        {
            cfgB.serial = value;
        }
        else if (arg == "--rate" && parseArgValue(i, argc, argv, value))
        {
            cfgA.rate = cfgB.rate = std::stod(value);
        }
        else if (arg == "--freq" && parseArgValue(i, argc, argv, value))
        {
            cfgA.freq = cfgB.freq = std::stod(value);
        }
        else if (arg == "--duration" && parseArgValue(i, argc, argv, value))
        {
            cfgA.durationSec = cfgB.durationSec = std::stod(value);
        }
        else if (arg == "--num-elems" && parseArgValue(i, argc, argv, value))
        {
            cfgA.numElems = cfgB.numElems = static_cast<size_t>(std::stoul(value));
        }
        else if (arg == "--timeout-us" && parseArgValue(i, argc, argv, value))
        {
            cfgA.timeoutUs = cfgB.timeoutUs = static_cast<long>(std::stol(value));
        }
        else if (arg == "--max-timeouts" && parseArgValue(i, argc, argv, value))
        {
            cfgA.maxTimeouts = cfgB.maxTimeouts = std::stoi(value);
        }
        else if (arg == "--format" && parseArgValue(i, argc, argv, value))
        {
            cfgA.format = cfgB.format = value;
        }
        else
        {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
            return 2;
        }
    }

    if (cfgA.serial.empty() || cfgB.serial.empty())
    {
        std::cerr << "Usage: --serial-a SERIAL --serial-b SERIAL [--rate Sps] [--freq Hz] [--duration Sec]\n";
        std::cerr << "       [--num-elems N] [--timeout-us Us] [--max-timeouts N] [--format CS16|CF32]" << std::endl;
        return 2;
    }

    StreamStats statsA;
    StreamStats statsB;
    std::thread threadA([&]() { statsA = run_stream(cfgA); });
    std::thread threadB([&]() { statsB = run_stream(cfgB); });

    threadA.join();
    threadB.join();

    std::cout << "Device A (" << cfgA.serial << "): reads=" << statsA.reads
              << " samples=" << statsA.samples
              << " timeouts=" << statsA.timeouts
              << " errors=" << statsA.errors << std::endl;
    std::cout << "Device B (" << cfgB.serial << "): reads=" << statsB.reads
              << " samples=" << statsB.samples
              << " timeouts=" << statsB.timeouts
              << " errors=" << statsB.errors << std::endl;

    if (statsA.errors != 0 || statsB.errors != 0)
    {
        std::cerr << "HIL test failed: errors detected (A=" << statsA.lastError
                  << ", B=" << statsB.lastError << ")" << std::endl;
        return 1;
    }

    return 0;
}
