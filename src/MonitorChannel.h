#pragma once
#include "SerialWorker.h"
#include "PrevacProtocol.h" 
#include "ComPortList.h"
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include "imgui.h"

class MonitorChannel
{
public:
    explicit MonitorChannel(int id);
    ~MonitorChannel();

    void Update(std::ofstream* sharedLog, bool logging, const char* label); // call once per frame: polls worker, appends data, writes log rows
    void Render(float width, float height, bool showPlots = true);
    int  Id() const { return m_id; }
    void PlotSeries(const ImVec4& color, int rateWindow); // draws this channel into the currently open plot    bool IsPoppedOut() const { return m_poppedOut; }
    bool WantsRemoval() const { return m_markedForRemoval; }
    bool IsPoppedOut() const { return m_poppedOut; }

private:
    void Connect();
    void Disconnect();

    int m_id;
    char m_comPortBuf[32];
    uint8_t m_deviceAddress = 0xC8;
    uint8_t m_sampleFreqCode = 1;
    uint8_t m_lastDeviceId = 0;

    SerialWorker m_worker;
    bool m_connected = false;
    std::string m_lastError;

    std::vector<double> m_time_s, m_frequency_hz, m_rate5, m_rate30, m_rate60;
    std::chrono::steady_clock::time_point m_appStart;
    
    std::vector<std::string> m_availablePorts;

    float  m_freqWindowHz = 0.5f;
    bool m_sensorConnected = true;
    bool m_poppedOut = false;
    bool m_markedForRemoval = false;

    Prevac::FrameDebugInfo m_latestDebug;
};