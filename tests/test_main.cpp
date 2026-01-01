#include "SoapySDRPlay.hpp"

#include <cmath>
#include <iostream>
#include <string>

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

int main()
{
    test_hwver_mappings();
    test_rspduo_mode_mappings();
    test_bandwidth_mappings();
    test_stream_defaults();

    if (g_stats.failed != 0)
    {
        std::cerr << "Tests failed: " << g_stats.failed << " of " << g_stats.total << std::endl;
        return 1;
    }

    std::cout << "All tests passed (" << g_stats.total << " assertions)." << std::endl;
    return 0;
}
