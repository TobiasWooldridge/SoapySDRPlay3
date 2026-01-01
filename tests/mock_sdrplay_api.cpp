#include <sdrplay_api.h>

#include <cstring>
#include <mutex>

namespace {

struct MockDeviceState
{
    sdrplay_api_DeviceT device;
    sdrplay_api_DeviceParamsT params;
    sdrplay_api_DevParamsT devParams;
    sdrplay_api_RxChannelParamsT rxA;
    sdrplay_api_RxChannelParamsT rxB;
    bool selected = false;
    bool initialized = false;
};

std::once_flag g_init_flag;
MockDeviceState g_devices[2];
sdrplay_api_ErrorInfoT g_last_error{};
sdrplay_api_CallbackFnsT g_callbacks{};
void *g_cb_context = nullptr;
std::mutex g_mutex;

void init_channel_defaults(sdrplay_api_RxChannelParamsT &channel)
{
    std::memset(&channel, 0, sizeof(channel));
    channel.tunerParams.bwType = sdrplay_api_BW_0_200;
    channel.tunerParams.ifType = sdrplay_api_IF_Zero;
    channel.tunerParams.rfFreq.rfHz = 200000000.0;
    channel.tunerParams.gain.gRdB = 50;
    channel.tunerParams.gain.LNAstate = 0;
    channel.ctrlParams.dcOffset.DCenable = 1;
    channel.ctrlParams.dcOffset.IQenable = 1;
    channel.ctrlParams.decimation.enable = 0;
    channel.ctrlParams.decimation.decimationFactor = 1;
    channel.ctrlParams.agc.enable = sdrplay_api_AGC_50HZ;
    channel.ctrlParams.agc.setPoint_dBfs = -60;
}

void init_device(MockDeviceState &state, const char *serial)
{
    std::memset(&state, 0, sizeof(state));
    std::strncpy(state.device.SerNo, serial, sizeof(state.device.SerNo) - 1);
    state.device.hwVer = SDRPLAY_RSPdxR2_ID;
    state.device.tuner = sdrplay_api_Tuner_A;
    state.device.rspDuoMode = sdrplay_api_RspDuoMode_Unknown;
    state.device.valid = 1;
    state.device.rspDuoSampleFreq = 0;
    state.device.dev = reinterpret_cast<HANDLE>(&state);

    state.params.devParams = &state.devParams;
    state.params.rxChannelA = &state.rxA;
    state.params.rxChannelB = &state.rxB;

    std::memset(&state.devParams, 0, sizeof(state.devParams));
    state.devParams.fsFreq.fsHz = 2000000.0;
    state.devParams.mode = sdrplay_api_ISOCH;

    init_channel_defaults(state.rxA);
    init_channel_defaults(state.rxB);
}

void init_mock_devices()
{
    std::call_once(g_init_flag, []() {
        init_device(g_devices[0], "TEST0001");
        init_device(g_devices[1], "TEST0002");
        std::memset(&g_last_error, 0, sizeof(g_last_error));
    });
}

MockDeviceState *find_device_by_serial(const char *serial)
{
    for (auto &state : g_devices)
    {
        if (std::strncmp(state.device.SerNo, serial, sizeof(state.device.SerNo)) == 0)
        {
            return &state;
        }
    }
    return nullptr;
}

MockDeviceState *find_device_by_handle(HANDLE dev)
{
    for (auto &state : g_devices)
    {
        if (state.device.dev == dev)
        {
            return &state;
        }
    }
    return nullptr;
}

} // namespace

extern "C" {

sdrplay_api_ErrT sdrplay_api_Open(void)
{
    init_mock_devices();
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Close(void)
{
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_ApiVersion(float *apiVer)
{
    if (apiVer == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    *apiVer = SDRPLAY_API_VERSION;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_LockDeviceApi(void)
{
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_UnlockDeviceApi(void)
{
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_GetDevices(sdrplay_api_DeviceT *devices,
                                        unsigned int *numDevs,
                                        unsigned int maxDevs)
{
    if (devices == nullptr || numDevs == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    init_mock_devices();

    const unsigned int available = static_cast<unsigned int>(sizeof(g_devices) / sizeof(g_devices[0]));
    const unsigned int count = (available < maxDevs) ? available : maxDevs;
    for (unsigned int i = 0; i < count; i++)
    {
        devices[i] = g_devices[i].device;
    }
    *numDevs = count;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_SelectDevice(sdrplay_api_DeviceT *device)
{
    if (device == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    init_mock_devices();

    std::lock_guard<std::mutex> lock(g_mutex);
    MockDeviceState *state = find_device_by_serial(device->SerNo);
    if (state == nullptr)
    {
        return sdrplay_api_HwError;
    }
    state->selected = true;
    device->dev = state->device.dev;
    device->hwVer = state->device.hwVer;
    device->tuner = state->device.tuner;
    device->rspDuoMode = state->device.rspDuoMode;
    device->rspDuoSampleFreq = state->device.rspDuoSampleFreq;
    device->valid = 1;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_ReleaseDevice(sdrplay_api_DeviceT *device)
{
    if (device == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    init_mock_devices();

    std::lock_guard<std::mutex> lock(g_mutex);
    MockDeviceState *state = find_device_by_handle(device->dev);
    if (state == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    state->selected = false;
    return sdrplay_api_Success;
}

const char *sdrplay_api_GetErrorString(sdrplay_api_ErrT err)
{
    switch (err)
    {
    case sdrplay_api_Success:
        return "Success";
    case sdrplay_api_InvalidParam:
        return "InvalidParam";
    case sdrplay_api_NotInitialised:
        return "NotInitialised";
    case sdrplay_api_HwError:
        return "HwError";
    default:
        return "Unknown";
    }
}

sdrplay_api_ErrorInfoT *sdrplay_api_GetLastError(sdrplay_api_DeviceT *device)
{
    (void)device;
    return &g_last_error;
}

sdrplay_api_ErrT sdrplay_api_DebugEnable(HANDLE dev, sdrplay_api_DbgLvl_t dbgLvl)
{
    (void)dev;
    (void)dbgLvl;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_GetDeviceParams(HANDLE dev, sdrplay_api_DeviceParamsT **deviceParams)
{
    if (deviceParams == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    init_mock_devices();

    MockDeviceState *state = find_device_by_handle(dev);
    if (state == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    *deviceParams = &state->params;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Init(HANDLE dev, sdrplay_api_CallbackFnsT *callbackFns, void *cbContext)
{
    init_mock_devices();

    std::lock_guard<std::mutex> lock(g_mutex);
    MockDeviceState *state = find_device_by_handle(dev);
    if (state == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    if (callbackFns != nullptr)
    {
        g_callbacks = *callbackFns;
    }
    g_cb_context = cbContext;
    state->initialized = true;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Uninit(HANDLE dev)
{
    init_mock_devices();

    std::lock_guard<std::mutex> lock(g_mutex);
    MockDeviceState *state = find_device_by_handle(dev);
    if (state == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    state->initialized = false;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_Update(HANDLE dev,
                                    sdrplay_api_TunerSelectT tuner,
                                    sdrplay_api_ReasonForUpdateT reasonForUpdate,
                                    sdrplay_api_ReasonForUpdateExtension1T reasonForUpdateExt1)
{
    (void)dev;
    (void)tuner;
    (void)reasonForUpdate;
    (void)reasonForUpdateExt1;
    return sdrplay_api_Success;
}

sdrplay_api_ErrT sdrplay_api_SwapRspDuoActiveTuner(HANDLE dev,
                                                   sdrplay_api_TunerSelectT *currentTuner,
                                                   sdrplay_api_RspDuo_AmPortSelectT tuner1AmPortSel)
{
    (void)dev;
    (void)tuner1AmPortSel;
    if (currentTuner == nullptr)
    {
        return sdrplay_api_InvalidParam;
    }
    *currentTuner = (*currentTuner == sdrplay_api_Tuner_A) ? sdrplay_api_Tuner_B : sdrplay_api_Tuner_A;
    return sdrplay_api_Success;
}

} // extern "C"
