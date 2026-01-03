/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 - Health monitoring and recovery for SoapySDRPlay3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapySDRPlay.hpp"
#include <cstdlib>

/*******************************************************************
 * Health Status API
 ******************************************************************/

DeviceHealthStatus SoapySDRPlay::getHealthStatus() const
{
    std::lock_guard<std::mutex> lock(healthInfoMutex);
    return healthInfo.status;
}

HealthInfo SoapySDRPlay::getHealthInfo() const
{
    std::lock_guard<std::mutex> lock(healthInfoMutex);
    return healthInfo;
}

void SoapySDRPlay::registerHealthCallback(std::function<void(DeviceHealthStatus)> callback)
{
    std::lock_guard<std::mutex> lock(healthCallbacksMutex);
    healthCallbacks.push_back(callback);
}

void SoapySDRPlay::updateHealthStatus(DeviceHealthStatus newStatus)
{
    DeviceHealthStatus oldStatus;
    {
        std::lock_guard<std::mutex> lock(healthInfoMutex);
        oldStatus = healthInfo.status;
        healthInfo.status = newStatus;
        if (newStatus == DeviceHealthStatus::Healthy) {
            healthInfo.lastHealthyTime = std::chrono::steady_clock::now();
        }
    }

    if (oldStatus != newStatus) {
        notifyHealthCallbacks(newStatus);
    }
}

void SoapySDRPlay::notifyHealthCallbacks(DeviceHealthStatus status)
{
    std::vector<std::function<void(DeviceHealthStatus)>> callbacks;
    {
        std::lock_guard<std::mutex> lock(healthCallbacksMutex);
        callbacks = healthCallbacks;  // Copy to avoid holding lock during callbacks
    }

    for (auto& callback : callbacks) {
        try {
            callback(status);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

/*******************************************************************
 * Watchdog Configuration API
 ******************************************************************/

WatchdogConfig SoapySDRPlay::getWatchdogConfig() const
{
    return watchdogConfig;
}

void SoapySDRPlay::setWatchdogConfig(const WatchdogConfig& config)
{
    watchdogConfig = config;
}

/*******************************************************************
 * Settings Cache - Save and Restore
 ******************************************************************/

void SoapySDRPlay::saveCurrentSettings()
{
    std::lock_guard<std::mutex> cacheLock(settingsCacheMutex);
    std::lock_guard<std::mutex> stateLock(_general_state_mutex);

    // Save frequency settings
    settingsCache.rfFrequencyHz = chParams->tunerParams.rfFreq.rfHz;
    if (deviceParams && deviceParams->devParams) {
        settingsCache.ppmCorrection = deviceParams->devParams->ppm;
    }

    // Save gain settings
    settingsCache.lnaState = chParams->tunerParams.gain.LNAstate;
    settingsCache.ifGainReduction = chParams->tunerParams.gain.gRdB;
    settingsCache.agcEnabled = (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_CTRL_EN);
    settingsCache.agcSetPoint = chParams->ctrlParams.agc.setPoint_dBfs;

    // Save sample rate (from device params if available)
    if (deviceParams && deviceParams->devParams) {
        settingsCache.sampleRate = deviceParams->devParams->fsFreq.fsHz;
    }

    // Save decimation
    settingsCache.decimationEnabled = (chParams->ctrlParams.decimation.enable != 0);
    settingsCache.decimationFactor = chParams->ctrlParams.decimation.decimationFactor;

    // Save DC/IQ correction
    settingsCache.dcCorrectionEnabled = (chParams->ctrlParams.dcOffset.DCenable != 0);
    settingsCache.iqCorrectionEnabled = (chParams->ctrlParams.dcOffset.IQenable != 0);

    // Save device-specific settings based on hardware version
    if (deviceParams && deviceParams->devParams) {
        switch (device.hwVer) {
            case SDRPLAY_RSP2_ID:
                settingsCache.biasTEnabled = (chParams->rsp2TunerParams.biasTEnable != 0);
                settingsCache.rfNotchEnabled = (chParams->rsp2TunerParams.rfNotchEnable != 0);
                settingsCache.extRefEnabled = (deviceParams->devParams->rsp2Params.extRefOutputEn != 0);
                break;

            case SDRPLAY_RSPduo_ID:
                settingsCache.biasTEnabled = (chParams->rspDuoTunerParams.biasTEnable != 0);
                settingsCache.rfNotchEnabled = (chParams->rspDuoTunerParams.rfNotchEnable != 0);
                settingsCache.dabNotchEnabled = (chParams->rspDuoTunerParams.rfDabNotchEnable != 0);
                settingsCache.extRefEnabled = (deviceParams->devParams->rspDuoParams.extRefOutputEn != 0);
                break;

            case SDRPLAY_RSP1A_ID:
            case SDRPLAY_RSP1B_ID:
                settingsCache.biasTEnabled = (chParams->rsp1aTunerParams.biasTEnable != 0);
                settingsCache.rfNotchEnabled = (deviceParams->devParams->rsp1aParams.rfNotchEnable != 0);
                settingsCache.dabNotchEnabled = (deviceParams->devParams->rsp1aParams.rfDabNotchEnable != 0);
                break;

            case SDRPLAY_RSPdx_ID:
            case SDRPLAY_RSPdxR2_ID:
                settingsCache.biasTEnabled = (deviceParams->devParams->rspDxParams.biasTEnable != 0);
                settingsCache.rfNotchEnabled = (deviceParams->devParams->rspDxParams.rfNotchEnable != 0);
                settingsCache.dabNotchEnabled = (deviceParams->devParams->rspDxParams.rfDabNotchEnable != 0);
                settingsCache.hdrEnabled = (deviceParams->devParams->rspDxParams.hdrEnable != 0);
                break;

            default:
                break;
        }
    }

    // Save antenna name
    settingsCache.antennaName = getAntenna(SOAPY_SDR_RX, 0);

    settingsCache.savedAt = std::chrono::steady_clock::now();
    settingsCache.isValid = true;

    SoapySDR_log(SOAPY_SDR_DEBUG, "Settings saved to cache for recovery");
}

bool SoapySDRPlay::restoreSettings()
{
    std::lock_guard<std::mutex> cacheLock(settingsCacheMutex);

    if (!settingsCache.isValid) {
        SoapySDR_log(SOAPY_SDR_WARNING, "Cannot restore settings - cache is not valid");
        return false;
    }

    bool success = true;

    try {
        // Restore frequency
        setFrequency(SOAPY_SDR_RX, 0, settingsCache.rfFrequencyHz);
    } catch (const std::exception& e) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Failed to restore frequency: %s", e.what());
        success = false;
    }

    try {
        // Restore gain mode (AGC)
        setGainMode(SOAPY_SDR_RX, 0, settingsCache.agcEnabled);

        if (!settingsCache.agcEnabled) {
            // Restore manual gain settings
            setGain(SOAPY_SDR_RX, 0, "IFGR", settingsCache.ifGainReduction);
            setGain(SOAPY_SDR_RX, 0, "RFGR", settingsCache.lnaState);
        }
    } catch (const std::exception& e) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Failed to restore gain: %s", e.what());
        success = false;
    }

    try {
        // Restore AGC setpoint via writeSetting
        writeSetting("agc_setpoint", std::to_string(settingsCache.agcSetPoint));
    } catch (const std::exception& e) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Failed to restore AGC setpoint: %s", e.what());
        success = false;
    }

    try {
        // Restore antenna
        if (!settingsCache.antennaName.empty()) {
            setAntenna(SOAPY_SDR_RX, 0, settingsCache.antennaName);
        }
    } catch (const std::exception& e) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Failed to restore antenna: %s", e.what());
        success = false;
    }

    // Restore device-specific settings
    try {
        writeSetting("biasT_ctrl", settingsCache.biasTEnabled ? "true" : "false");
        writeSetting("rfnotch_ctrl", settingsCache.rfNotchEnabled ? "true" : "false");
        writeSetting("dabnotch_ctrl", settingsCache.dabNotchEnabled ? "true" : "false");

        if (device.hwVer == SDRPLAY_RSPdx_ID || device.hwVer == SDRPLAY_RSPdxR2_ID) {
            writeSetting("hdr_ctrl", settingsCache.hdrEnabled ? "true" : "false");
        }
    } catch (const std::exception& e) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Failed to restore device-specific settings: %s", e.what());
        success = false;
    }

    SoapySDR_log(success ? SOAPY_SDR_INFO : SOAPY_SDR_WARNING,
                 success ? "Settings restored from cache" : "Some settings failed to restore");

    return success;
}

void SoapySDRPlay::invalidateSettingsCache()
{
    std::lock_guard<std::mutex> lock(settingsCacheMutex);
    settingsCache.isValid = false;
}

/*******************************************************************
 * Watchdog Thread
 ******************************************************************/

void SoapySDRPlay::startWatchdog()
{
    if (watchdogRunning.exchange(true)) {
        return;  // Already running
    }

    watchdogShutdown = false;
    watchdogThread = std::thread(&SoapySDRPlay::watchdogThreadFunc, this);

    SoapySDR_log(SOAPY_SDR_DEBUG, "Watchdog thread started");
}

void SoapySDRPlay::stopWatchdog()
{
    if (!watchdogRunning.load()) {
        return;  // Not running
    }

    watchdogShutdown = true;

    if (watchdogThread.joinable()) {
        watchdogThread.join();
    }

    watchdogRunning = false;
    SoapySDR_log(SOAPY_SDR_DEBUG, "Watchdog thread stopped");
}

void SoapySDRPlay::watchdogThreadFunc()
{
    SoapySDR_log(SOAPY_SDR_DEBUG, "Watchdog thread running");

    while (!watchdogShutdown.load())
    {
        // Sleep for check interval
        std::this_thread::sleep_for(
            std::chrono::milliseconds(watchdogConfig.healthCheckIntervalMs));

        if (watchdogShutdown.load()) {
            break;
        }

        // Skip if streaming is not active or device is already unavailable
        if (!streamActive.load() || device_unavailable.load()) {
            continue;
        }

        // Check each active stream for stale callbacks
        bool anyStale = false;
        {
            std::lock_guard<std::mutex> lock(_streams_mutex);
            auto now = std::chrono::steady_clock::now();

            for (int ch = 0; ch < 2; ch++)
            {
                auto* stream = _streams[ch];
                if (!stream) continue;

                uint64_t currentTicks = stream->lastCallbackTicks.load();

                // Check if callbacks have stopped
                if (currentTicks == stream->lastWatchdogTicks)
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - stream->lastCallbackTime).count();

                    if (elapsed > watchdogConfig.callbackTimeoutMs)
                    {
                        anyStale = true;
                        SoapySDR_logf(SOAPY_SDR_WARNING,
                            "Stream %d: no callbacks for %ld ms - stream may be stale",
                            ch, (long)elapsed);
                    }
                }
                else
                {
                    // Update tracking - callbacks are arriving
                    stream->lastWatchdogTicks = currentTicks;
                    stream->lastCallbackTime = now;
                }
            }
        }

        // Update health status
        if (anyStale) {
            updateHealthStatus(DeviceHealthStatus::Stale);

            // Attempt recovery if auto-recover is enabled
            if (watchdogConfig.autoRecover) {
                handleStaleStream();
            }
        } else if (streamActive.load()) {
            // Update callback statistics
            {
                std::lock_guard<std::mutex> lock(healthInfoMutex);
                // Calculate callback rate from streams
                uint64_t totalTicks = 0;
                std::lock_guard<std::mutex> slock(_streams_mutex);
                for (int ch = 0; ch < 2; ch++) {
                    if (_streams[ch]) {
                        totalTicks += _streams[ch]->lastCallbackTicks.load();
                    }
                }
                healthInfo.callbackCount = totalTicks;
            }
            updateHealthStatus(DeviceHealthStatus::Healthy);
        }
    }

    SoapySDR_log(SOAPY_SDR_DEBUG, "Watchdog thread exiting");
}

/*******************************************************************
 * Stream Recovery
 ******************************************************************/

void SoapySDRPlay::handleStaleStream()
{
    // Check if we've exceeded max recovery attempts
    if (recoveryAttemptCount.load() >= watchdogConfig.maxRecoveryAttempts) {
        SoapySDR_log(SOAPY_SDR_ERROR,
            "Max recovery attempts exceeded - manual intervention required");
        updateHealthStatus(DeviceHealthStatus::Failed);
        return;
    }

    // Check backoff timing
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastRecoveryAttempt).count();

    int backoffMs = watchdogConfig.recoveryBackoffMs * (1 << recoveryAttemptCount.load());
    if (elapsed < backoffMs) {
        return;  // Wait for backoff period
    }

    RecoveryResult result = attemptStreamRecovery();

    if (result == RecoveryResult::Success) {
        SoapySDR_log(SOAPY_SDR_INFO, "Stream recovery successful");
        recoveryAttemptCount = 0;  // Reset on success
    } else if (result == RecoveryResult::ServiceDown && watchdogConfig.restartServiceOnFailure) {
        SoapySDR_log(SOAPY_SDR_WARNING, "Attempting service restart...");
        if (restartService()) {
            // Try recovery again after service restart
            std::this_thread::sleep_for(std::chrono::seconds(2));
            attemptStreamRecovery();
        }
    }
}

RecoveryResult SoapySDRPlay::attemptStreamRecovery()
{
    if (recoveryInProgress.exchange(true)) {
        return RecoveryResult::InProgress;  // Another recovery in progress
    }

    SoapySDR_log(SOAPY_SDR_WARNING, "Attempting stream recovery...");
    updateHealthStatus(DeviceHealthStatus::Recovering);

    // Save current settings before recovery
    saveCurrentSettings();

    // Remember active streams
    std::vector<size_t> activeChannels;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        for (size_t i = 0; i < 2; i++) {
            if (_streams[i] != nullptr) {
                activeChannels.push_back(i);
            }
        }
    }

    RecoveryResult result = RecoveryResult::Success;

    // Stream recovery is complex because the SDRplay API callbacks are registered
    // during sdrplay_api_Init() and cannot be easily re-registered without closing
    // and reopening the device.
    //
    // For now, we detect the stale stream and notify the application via health status.
    // The application should close and reopen the stream to recover.
    //
    // Future improvement: Implement full device close/reopen cycle with settings restore.

    SoapySDR_log(SOAPY_SDR_WARNING,
        "Stream appears stale. Application should close and reopen the stream to recover.");
    SoapySDR_log(SOAPY_SDR_INFO,
        "Hint: Close the stream (closeStream), then reopen (setupStream + activateStream)");

    // Update health status to signal application
    updateHealthStatus(DeviceHealthStatus::Stale);

    // Check if service is responsive
    if (!isServiceResponsive()) {
        SoapySDR_log(SOAPY_SDR_WARNING,
            "SDRplay service appears unresponsive. Consider restarting the service.");

        if (watchdogConfig.restartServiceOnFailure) {
            SoapySDR_log(SOAPY_SDR_INFO, "Attempting to restart SDRplay service...");
            if (restartService()) {
                resetServiceHealthTracking();
                result = RecoveryResult::Success;
                SoapySDR_log(SOAPY_SDR_INFO, "Service restart requested. Stream should be reopened.");
            } else {
                result = RecoveryResult::ServiceDown;
            }
        } else {
            result = RecoveryResult::ServiceDown;
        }
    } else {
        // Service is responsive but stream is stale - likely a transient issue
        result = RecoveryResult::FailedInit;  // Signal that app-level recovery is needed
    }

    recoveryInProgress = false;
    recoveryAttemptCount++;
    lastRecoveryAttempt = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(healthInfoMutex);
        healthInfo.recoveryAttempts = recoveryAttemptCount.load();
        if (result == RecoveryResult::Success) {
            healthInfo.successfulRecoveries++;
        }
    }

    return result;
}

/*******************************************************************
 * Manual Recovery Controls
 ******************************************************************/

bool SoapySDRPlay::triggerRecovery()
{
    RecoveryResult result = attemptStreamRecovery();
    return (result == RecoveryResult::Success);
}

/*******************************************************************
 * Service Control - Sudoers-Friendly
 *
 * These functions use external scripts that can be added to sudoers
 * for minimal privilege escalation:
 *
 * Add to /etc/sudoers.d/sdrplay:
 *   %sdrplay ALL=(ALL) NOPASSWD: /usr/local/bin/sdrplay-service-restart
 *   %sdrplay ALL=(ALL) NOPASSWD: /usr/local/bin/sdrplay-usb-reset
 *
 * Then add users to the 'sdrplay' group.
 ******************************************************************/

bool SoapySDRPlay::restartService()
{
    SoapySDR_log(SOAPY_SDR_WARNING, "Attempting to restart SDRplay service...");

    // First, try without sudo (in case script has setuid or user has permission)
    int result = std::system("sdrplay-service-restart 2>/dev/null");

    if (result != 0) {
        // Try with sudo (if user is in sudoers)
        result = std::system("sudo -n sdrplay-service-restart 2>/dev/null");
    }

    if (result == 0) {
        SoapySDR_log(SOAPY_SDR_INFO, "SDRplay service restart requested");
        // Wait for service to restart
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return true;
    }

    SoapySDR_log(SOAPY_SDR_ERROR,
        "Failed to restart SDRplay service - check sdrplay-service-restart script and sudoers");
    return false;
}

bool SoapySDRPlay::resetUSBDevice()
{
    if (!watchdogConfig.usbResetOnFailure) {
        SoapySDR_log(SOAPY_SDR_WARNING, "USB reset is disabled in watchdog config");
        return false;
    }

    SoapySDR_logf(SOAPY_SDR_WARNING, "Attempting USB reset for device %s...", serNo.c_str());

    // Build command with serial number
    std::string cmd = "sdrplay-usb-reset " + serNo + " 2>/dev/null";
    int result = std::system(cmd.c_str());

    if (result != 0) {
        // Try with sudo
        cmd = "sudo -n sdrplay-usb-reset " + serNo + " 2>/dev/null";
        result = std::system(cmd.c_str());
    }

    if (result == 0) {
        SoapySDR_log(SOAPY_SDR_INFO, "USB reset requested - waiting for device to re-enumerate");
        // Wait for USB re-enumeration
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return true;
    }

    SoapySDR_log(SOAPY_SDR_ERROR,
        "Failed to reset USB device - check sdrplay-usb-reset script and sudoers");
    return false;
}
