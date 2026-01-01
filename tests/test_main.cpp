#include "SoapySDRPlay.hpp"

#include <SoapySDR/Errors.hpp>

#include <cerrno>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

struct TestStats
{
    int total = 0;
    int failed = 0;
};

static TestStats g_stats;

static void recordFailure(const char *file, int line, const std::string &message)
{
    g_stats.failed++;
    std::cerr << file << ":" << line << " " << message << std::endl;
}

#define EXPECT_TRUE(expr) \
    do { \
        g_stats.total++; \
        if (!(expr)) { \
            recordFailure(__FILE__, __LINE__, std::string("EXPECT_TRUE failed: ") + #expr); \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        g_stats.total++; \
        const auto _a = (a); \
        const auto _b = (b); \
        if (!(_a == _b)) { \
            recordFailure(__FILE__, __LINE__, std::string("EXPECT_EQ failed: ") + #a + " != " + #b); \
        } \
    } while (0)

#define EXPECT_NEAR(a, b, tol) \
    do { \
        g_stats.total++; \
        const double _a = static_cast<double>(a); \
        const double _b = static_cast<double>(b); \
        if (std::fabs(_a - _b) > (tol)) { \
            recordFailure(__FILE__, __LINE__, std::string("EXPECT_NEAR failed: ") + #a + " ~= " + #b); \
        } \
    } while (0)

static void test_hwver_mappings()
{
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("RSP1"), SDRPLAY_RSP1_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("rsp1a"), SDRPLAY_RSP1A_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("RSP1B"), SDRPLAY_RSP1B_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("rsp2"), SDRPLAY_RSP2_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("RSPduo"), SDRPLAY_RSPduo_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("rspdx"), SDRPLAY_RSPdx_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("rspdx-r2"), SDRPLAY_RSPdxR2_ID);
    EXPECT_EQ(SoapySDRPlay::stringToHWVer("unknown"), 0);

    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSP1_ID), std::string("RSP1"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSP1A_ID), std::string("RSP1A"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSP1B_ID), std::string("RSP1B"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSP2_ID), std::string("RSP2"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSPduo_ID), std::string("RSPduo"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSPdx_ID), std::string("RSPdx"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(SDRPLAY_RSPdxR2_ID), std::string("RSPdx-R2"));
    EXPECT_EQ(SoapySDRPlay::HWVertoString(0), std::string(""));
}

static void test_rspduo_mode_mappings()
{
    EXPECT_EQ(SoapySDRPlay::stringToRSPDuoMode("Single Tuner"), sdrplay_api_RspDuoMode_Single_Tuner);
    EXPECT_EQ(SoapySDRPlay::stringToRSPDuoMode("dual tuner"), sdrplay_api_RspDuoMode_Dual_Tuner);
    EXPECT_EQ(SoapySDRPlay::stringToRSPDuoMode("Master"), sdrplay_api_RspDuoMode_Master);
    EXPECT_EQ(SoapySDRPlay::stringToRSPDuoMode("slave"), sdrplay_api_RspDuoMode_Slave);
    EXPECT_EQ(SoapySDRPlay::stringToRSPDuoMode("unknown"), sdrplay_api_RspDuoMode_Unknown);

    EXPECT_EQ(SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoMode_Single_Tuner), std::string("Single Tuner"));
    EXPECT_EQ(SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoMode_Dual_Tuner), std::string("Dual Tuner"));
    EXPECT_EQ(SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoMode_Master), std::string("Master"));
    EXPECT_EQ(SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoMode_Slave), std::string("Slave"));
    EXPECT_EQ(SoapySDRPlay::RSPDuoModetoString(sdrplay_api_RspDuoMode_Unknown), std::string(""));
}

static void test_bandwidth_mappings()
{
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(299999), sdrplay_api_BW_0_200);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(300000), sdrplay_api_BW_0_300);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(599999), sdrplay_api_BW_0_300);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(600000), sdrplay_api_BW_0_600);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(1535999), sdrplay_api_BW_0_600);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(1536000), sdrplay_api_BW_1_536);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(4999999), sdrplay_api_BW_1_536);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(5000000), sdrplay_api_BW_5_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(5999999), sdrplay_api_BW_5_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(6000000), sdrplay_api_BW_6_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(6999999), sdrplay_api_BW_6_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(7000000), sdrplay_api_BW_7_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(7999999), sdrplay_api_BW_7_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(8000000), sdrplay_api_BW_8_000);
    EXPECT_EQ(SoapySDRPlay::test_getBwEnumForRate(10000000), sdrplay_api_BW_8_000);

    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_0_200), 200000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_0_300), 300000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_0_600), 600000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_1_536), 1536000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_5_000), 5000000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_6_000), 6000000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_7_000), 7000000.0, 0.1);
    EXPECT_NEAR(SoapySDRPlay::test_getBwValueFromEnum(sdrplay_api_BW_8_000), 8000000.0, 0.1);
}

static void test_stream_defaults()
{
    const size_t numBuffers = 8;
    const unsigned long bufferLength = 4096;
    SoapySDRPlay::SoapySDRPlayStream stream(0, numBuffers, bufferLength);

    EXPECT_EQ(stream.channel, static_cast<size_t>(0));
    EXPECT_EQ(stream.head, static_cast<size_t>(0));
    EXPECT_EQ(stream.tail, static_cast<size_t>(0));
    EXPECT_EQ(stream.count, static_cast<size_t>(0));
    EXPECT_TRUE(stream.currentBuff == nullptr);
    EXPECT_TRUE(!stream.overflowEvent);
    EXPECT_EQ(stream.nElems.load(), static_cast<size_t>(0));
    EXPECT_EQ(stream.currentHandle, static_cast<size_t>(0));
    EXPECT_TRUE(!stream.reset.load());

    EXPECT_EQ(stream.shortBuffs.size(), numBuffers);
    EXPECT_EQ(stream.floatBuffs.size(), numBuffers);
    for (size_t i = 0; i < numBuffers; i++)
    {
        EXPECT_TRUE(stream.shortBuffs[i].capacity() >= bufferLength);
        EXPECT_TRUE(stream.floatBuffs[i].capacity() >= bufferLength);
    }
}

static bool create_dir(const std::string &path)
{
#ifdef _WIN32
    struct _stat st;
    if (_stat(path.c_str(), &st) == 0)
    {
        return (st.st_mode & _S_IFDIR) != 0;
    }
    if (_mkdir(path.c_str()) == 0)
    {
        return true;
    }
    return errno == EEXIST;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path.c_str(), 0755) == 0)
    {
        return true;
    }
    return errno == EEXIST;
#endif
}

class ScopedEnvVar
{
public:
    ScopedEnvVar(const std::string &key, const std::string &value)
        : key_(key)
    {
        const char *current = std::getenv(key.c_str());
        if (current != nullptr)
        {
            hadValue_ = true;
            oldValue_ = current;
        }
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar()
    {
#ifdef _WIN32
        if (hadValue_)
        {
            _putenv_s(key_.c_str(), oldValue_.c_str());
        }
        else
        {
            _putenv_s(key_.c_str(), "");
        }
#else
        if (hadValue_)
        {
            setenv(key_.c_str(), oldValue_.c_str(), 1);
        }
        else
        {
            unsetenv(key_.c_str());
        }
#endif
    }

private:
    std::string key_;
    bool hadValue_ = false;
    std::string oldValue_;
};

static void test_antenna_persistence()
{
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = getpid();
#endif
    std::string dir = "test-antenna-" + std::to_string(pid);
    EXPECT_TRUE(create_dir(dir));

    ScopedEnvVar env("SOAPY_SDRPLAY_CONFIG_DIR", dir);

    SoapySDR::Kwargs args;
    args["serial"] = "TEST0001";
    {
        SoapySDRPlay device(args);
        device.setAntenna(SOAPY_SDR_RX, 0, "Antenna B");
        EXPECT_EQ(device.getAntenna(SOAPY_SDR_RX, 0), std::string("Antenna B"));
    }
    {
        SoapySDRPlay device(args);
        EXPECT_EQ(device.getAntenna(SOAPY_SDR_RX, 0), std::string("Antenna B"));
    }
}

static void test_constructor_requires_serial()
{
    bool threw = false;
    try
    {
        SoapySDR::Kwargs args;
        SoapySDRPlay device(args);
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

static void test_device_construction_and_info()
{
    SoapySDR::Kwargs args;
    args["serial"] = "TEST0001";

    SoapySDRPlay device(args);
    EXPECT_EQ(device.getDriverKey(), std::string("SDRplay"));
    EXPECT_EQ(device.getHardwareKey(), std::string("RSPdx-R2"));

    auto info = device.getHardwareInfo();
    EXPECT_TRUE(info.count("sdrplay_api_api_version") != 0);
    EXPECT_TRUE(info.count("sdrplay_api_hw_version") != 0);
    EXPECT_EQ(device.getNumChannels(SOAPY_SDR_RX), static_cast<size_t>(1));
}

static void test_readStream_timeout_when_inactive()
{
    SoapySDR::Kwargs args;
    args["serial"] = "TEST0001";

    SoapySDRPlay device(args);
    SoapySDR::Stream *stream = device.setupStream(SOAPY_SDR_RX, "CS16");

    short buff[8] = {};
    void *buffs[] = { buff };
    int flags = 0;
    long long timeNs = 0;
    int ret = device.readStream(stream, buffs, 4, flags, timeNs, 1000);
    EXPECT_EQ(ret, SOAPY_SDR_TIMEOUT);

    device.closeStream(stream);
}

static void test_stream_read_cs16()
{
    SoapySDR::Kwargs args;
    args["serial"] = "TEST0001";

    SoapySDRPlay device(args);
    SoapySDR::Stream *stream = device.setupStream(SOAPY_SDR_RX, "CS16");
    EXPECT_EQ(device.activateStream(stream), 0);

    auto *playStream = reinterpret_cast<SoapySDRPlay::SoapySDRPlayStream *>(stream);
    playStream->reset = false;
    const unsigned int preSamples = 4;
    const unsigned int flushSamples = DEFAULT_BUFFER_LENGTH - preSamples;
    short xi[preSamples] = { 1, 2, 3, 4 };
    short xq[preSamples] = { 5, 6, 7, 8 };
    std::vector<short> xiFlush(flushSamples, 0);
    std::vector<short> xqFlush(flushSamples, 0);
    sdrplay_api_StreamCbParamsT params{};
    params.numSamples = preSamples;
    {
        std::lock_guard<std::mutex> lock(playStream->mutex);
        device.rx_callback(xi, xq, &params, preSamples, playStream);
        params.numSamples = flushSamples;
        device.rx_callback(xiFlush.data(), xqFlush.data(), &params, flushSamples, playStream);
    }

    short buff[8] = {};
    void *buffs[] = { buff };
    int flags = 0;
    long long timeNs = 0;
    int ret = device.readStream(stream, buffs, preSamples, flags, timeNs, 100000);
    EXPECT_EQ(ret, static_cast<int>(preSamples));
    EXPECT_EQ(buff[0], 1);
    EXPECT_EQ(buff[1], 5);
    EXPECT_EQ(buff[2], 2);
    EXPECT_EQ(buff[3], 6);
    EXPECT_EQ(buff[4], 3);
    EXPECT_EQ(buff[5], 7);
    EXPECT_EQ(buff[6], 4);
    EXPECT_EQ(buff[7], 8);

    device.closeStream(stream);
}

static void test_stream_read_cf32()
{
    SoapySDR::Kwargs args;
    args["serial"] = "TEST0002";

    SoapySDRPlay device(args);
    SoapySDR::Stream *stream = device.setupStream(SOAPY_SDR_RX, "CF32");
    EXPECT_EQ(device.activateStream(stream), 0);

    auto *playStream = reinterpret_cast<SoapySDRPlay::SoapySDRPlayStream *>(stream);
    playStream->reset = false;
    const unsigned int preSamples = 2;
    const unsigned int flushSamples = DEFAULT_BUFFER_LENGTH - preSamples;
    short xi[preSamples] = { 16384, -16384 };
    short xq[preSamples] = { 8192, -8192 };
    std::vector<short> xiFlush(flushSamples, 0);
    std::vector<short> xqFlush(flushSamples, 0);
    sdrplay_api_StreamCbParamsT params{};
    params.numSamples = preSamples;
    {
        std::lock_guard<std::mutex> lock(playStream->mutex);
        device.rx_callback(xi, xq, &params, preSamples, playStream);
        params.numSamples = flushSamples;
        device.rx_callback(xiFlush.data(), xqFlush.data(), &params, flushSamples, playStream);
    }

    float buff[4] = {};
    void *buffs[] = { buff };
    int flags = 0;
    long long timeNs = 0;
    int ret = device.readStream(stream, buffs, preSamples, flags, timeNs, 100000);
    EXPECT_EQ(ret, static_cast<int>(preSamples));
    EXPECT_NEAR(buff[0], 0.5, 1e-6);
    EXPECT_NEAR(buff[1], 0.25, 1e-6);
    EXPECT_NEAR(buff[2], -0.5, 1e-6);
    EXPECT_NEAR(buff[3], -0.25, 1e-6);

    device.closeStream(stream);
}

int main()
{
    std::string baseDir = "test-config";
    create_dir(baseDir);
    ScopedEnvVar env("SOAPY_SDRPLAY_CONFIG_DIR", baseDir);

    SoapySDRPlay::sdrplay_api::get_instance();

    test_hwver_mappings();
    test_rspduo_mode_mappings();
    test_bandwidth_mappings();
    test_stream_defaults();
    test_antenna_persistence();
    test_constructor_requires_serial();
    test_device_construction_and_info();
    test_readStream_timeout_when_inactive();
    test_stream_read_cs16();
    test_stream_read_cf32();

    if (g_stats.failed != 0)
    {
        std::cerr << "Tests failed: " << g_stats.failed << " of " << g_stats.total << std::endl;
        return 1;
    }

    std::cout << "All tests passed (" << g_stats.total << " assertions)." << std::endl;
    return 0;
}
