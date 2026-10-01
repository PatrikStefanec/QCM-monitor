#pragma once
#include "SerialPort.h"
#include "PrevacProtocol.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

// Runs serial polling on a background thread so the UI thread never blocks.
class SerialWorker
{
public:
    ~SerialWorker() { Stop(); }

    bool Start(const std::string& portName, uint8_t deviceAddress, uint8_t sampleFreqCode)
    {
        Stop(); // ensure clean state

        if (!m_port.Open(portName, 57600))
            return false;

        m_deviceAddress = deviceAddress;
        m_sampleFreqCode = sampleFreqCode;
        m_running = true;
        m_thread = std::thread(&SerialWorker::ThreadFunc, this);
        return true;
    }

    void Stop()
    {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
        m_port.Close();
    }

    bool IsRunning() const { return m_running; }

    // Called from the UI thread once per frame: grabs the latest reading, if any,
    // since the last call. Returns false if nothing new is available.
    bool PollLatest(Prevac::FrequencyReading& outReading, Prevac::FrameDebugInfo& outDebug)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasNew)
            return false;
        outReading = m_latestReading;
        outDebug = m_latestDebug;
        m_hasNew = false;
        return true;
    }

    int GetPollCount() const { return m_pollCount; }
    int GetSuccessCount() const { return m_successCount; }

private:
    void ThreadFunc()
    {
        while (m_running)
        {
            Prevac::FrameDebugInfo dbg;
            auto reading = Prevac::ReadFrequency(m_port, m_deviceAddress, m_sampleFreqCode, &dbg);
            m_pollCount++;

            if (reading)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_latestReading = *reading;
                m_latestDebug = dbg;
                m_hasNew = true;
                m_successCount++;
            }
            else
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_latestDebug = dbg;
                m_hasNew = true; // still surface debug info even on failure
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    SerialPort m_port;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    uint8_t m_deviceAddress = 0xC8;
    uint8_t m_sampleFreqCode = 1;

    std::mutex m_mutex;
    bool m_hasNew = false;
    Prevac::FrequencyReading m_latestReading{};
    Prevac::FrameDebugInfo m_latestDebug{};

    std::atomic<int> m_pollCount{ 0 };
    std::atomic<int> m_successCount{ 0 };
};