/*
 * Stress Test Framework for SoapySDRPlay3
 * Provides utilities for running stress tests with real hardware or mock API
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Test result structure
struct StressTestResult
{
    std::string testName;
    bool passed = false;
    int iterations = 0;
    int failures = 0;
    int timeouts = 0;
    double durationSec = 0.0;
    std::string failureReason;

    // Metrics
    size_t totalSamples = 0;
    size_t totalReads = 0;
    double avgLatencyMs = 0.0;
    double maxLatencyMs = 0.0;
    size_t peakMemoryBytes = 0;
    int fileDescriptorCount = 0;
};

// Metrics collector for long-running tests
class MetricsCollector
{
public:
    void recordLatency(double ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        totalLatency_ += ms;
        latencyCount_++;
        if (ms > maxLatency_)
        {
            maxLatency_ = ms;
        }
    }

    void recordSamples(size_t count)
    {
        totalSamples_.fetch_add(count, std::memory_order_relaxed);
    }

    void recordRead()
    {
        totalReads_.fetch_add(1, std::memory_order_relaxed);
    }

    void recordTimeout()
    {
        timeouts_.fetch_add(1, std::memory_order_relaxed);
    }

    void recordError()
    {
        errors_.fetch_add(1, std::memory_order_relaxed);
    }

    double getAverageLatencyMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return latencyCount_ > 0 ? totalLatency_ / latencyCount_ : 0.0;
    }

    double getMaxLatencyMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return maxLatency_;
    }

    size_t getTotalSamples() const { return totalSamples_.load(std::memory_order_relaxed); }
    size_t getTotalReads() const { return totalReads_.load(std::memory_order_relaxed); }
    int getTimeouts() const { return timeouts_.load(std::memory_order_relaxed); }
    int getErrors() const { return errors_.load(std::memory_order_relaxed); }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        totalLatency_ = 0.0;
        maxLatency_ = 0.0;
        latencyCount_ = 0;
        totalSamples_ = 0;
        totalReads_ = 0;
        timeouts_ = 0;
        errors_ = 0;
    }

private:
    mutable std::mutex mutex_;
    double totalLatency_ = 0.0;
    double maxLatency_ = 0.0;
    size_t latencyCount_ = 0;
    std::atomic<size_t> totalSamples_{0};
    std::atomic<size_t> totalReads_{0};
    std::atomic<int> timeouts_{0};
    std::atomic<int> errors_{0};
};

// Timer utility
class ScopedTimer
{
public:
    ScopedTimer() : start_(std::chrono::steady_clock::now()) {}

    double elapsedMs() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

    double elapsedSec() const
    {
        return elapsedMs() / 1000.0;
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// Test runner base class
class StressTestRunner
{
public:
    using TestFunc = std::function<StressTestResult()>;

    void addTest(const std::string &name, TestFunc func)
    {
        tests_.push_back({name, func});
    }

    std::vector<StressTestResult> runAll()
    {
        std::vector<StressTestResult> results;

        std::cout << "\n====== Stress Test Suite ======\n" << std::endl;

        for (const auto &test : tests_)
        {
            std::cout << "[RUNNING] " << test.name << "..." << std::flush;

            ScopedTimer timer;
            StressTestResult result;

            try
            {
                result = test.func();
                result.testName = test.name;
                result.durationSec = timer.elapsedSec();
            }
            catch (const std::exception &e)
            {
                result.testName = test.name;
                result.passed = false;
                result.failureReason = std::string("Exception: ") + e.what();
                result.durationSec = timer.elapsedSec();
            }

            if (result.passed)
            {
                std::cout << "\r[PASSED]  " << test.name
                          << " (" << std::fixed << std::setprecision(2)
                          << result.durationSec << "s)\n";
            }
            else
            {
                std::cout << "\r[FAILED]  " << test.name
                          << " - " << result.failureReason << "\n";
            }

            results.push_back(result);
        }

        // Summary
        int passed = 0, failed = 0;
        for (const auto &r : results)
        {
            if (r.passed) passed++;
            else failed++;
        }

        std::cout << "\n====== Summary ======\n";
        std::cout << "Passed: " << passed << "/" << (passed + failed) << std::endl;

        if (failed > 0)
        {
            std::cout << "\nFailed tests:\n";
            for (const auto &r : results)
            {
                if (!r.passed)
                {
                    std::cout << "  - " << r.testName << ": " << r.failureReason << "\n";
                }
            }
        }

        return results;
    }

private:
    struct TestEntry
    {
        std::string name;
        TestFunc func;
    };
    std::vector<TestEntry> tests_;
};

// Utility function to get current timestamp
inline std::string getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Progress reporter for long-running tests
class ProgressReporter
{
public:
    ProgressReporter(double totalDurationSec, int reportIntervalSec = 10)
        : totalDuration_(totalDurationSec)
        , reportInterval_(reportIntervalSec)
        , lastReport_(std::chrono::steady_clock::now())
    {
    }

    void update(const MetricsCollector &metrics)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastReport_).count();

        if (elapsed >= reportInterval_)
        {
            lastReport_ = now;

            auto totalElapsed = timer_.elapsedSec();
            double progress = (totalElapsed / totalDuration_) * 100.0;

            std::cout << "[" << getCurrentTimestamp() << "] "
                      << std::fixed << std::setprecision(1) << progress << "% "
                      << "samples=" << metrics.getTotalSamples() << " "
                      << "reads=" << metrics.getTotalReads() << " "
                      << "timeouts=" << metrics.getTimeouts() << " "
                      << "errors=" << metrics.getErrors() << "\n" << std::flush;
        }
    }

private:
    ScopedTimer timer_;
    double totalDuration_;
    int reportInterval_;
    std::chrono::steady_clock::time_point lastReport_;
};

// Deadlock detector - spawns watchdog thread
class DeadlockDetector
{
public:
    DeadlockDetector(int timeoutSec, const std::string &operationName)
        : timeout_(timeoutSec)
        , operationName_(operationName)
        , alive_(true)
        , completed_(false)
    {
        watchdog_ = std::thread([this]() {
            auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::seconds(timeout_);

            while (alive_.load())
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    if (!completed_.load())
                    {
                        std::cerr << "\n[DEADLOCK] Operation '" << operationName_
                                  << "' exceeded " << timeout_ << "s timeout!\n";
                        deadlockDetected_ = true;
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    ~DeadlockDetector()
    {
        alive_ = false;
        if (watchdog_.joinable())
        {
            watchdog_.join();
        }
    }

    void markCompleted()
    {
        completed_ = true;
    }

    bool wasDeadlocked() const
    {
        return deadlockDetected_;
    }

private:
    int timeout_;
    std::string operationName_;
    std::atomic<bool> alive_;
    std::atomic<bool> completed_;
    std::atomic<bool> deadlockDetected_{false};
    std::thread watchdog_;
};
