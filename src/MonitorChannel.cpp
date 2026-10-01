#include "MonitorChannel.h"
#include "imgui.h"
#include "implot.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
#include <iostream>

MonitorChannel::MonitorChannel(int id) : m_id(id)
{
    snprintf(m_comPortBuf, sizeof(m_comPortBuf), "COM%d", id + 4);
    m_appStart = std::chrono::steady_clock::now();
    m_availablePorts = ListAvailableComPorts();
}

MonitorChannel::~MonitorChannel()
{
    m_worker.Stop(); // stop the polling thread if still running
    FlushLog();      // make sure any open log is saved
}

void MonitorChannel::Connect()
{
    if (m_worker.Start(m_comPortBuf, m_deviceAddress, m_sampleFreqCode))
    {
        m_connected = true;
        m_lastError.clear();
    }
    else
    {
        m_lastError = "Failed to open " + std::string(m_comPortBuf);
    }
}

void MonitorChannel::Disconnect()
{
    m_worker.Stop();
    m_connected = false;
    if (m_logging)
    {
        m_logging = false;
        if (m_logFile.is_open()) m_logFile.close();
        m_logSavedMessage = "Log \"" + m_logFilePath + "\" was saved successfully (disconnected).";
    }
}

void MonitorChannel::ToggleLogging()
{
    if (!m_logging)
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tmStruct;
        localtime_s(&tmStruct, &t);

        std::ostringstream oss;
        oss << "tm13_ch" << m_id << "_log_" << std::put_time(&tmStruct, "%Y%m%d_%H%M%S") << ".csv";
        m_logFilePath = oss.str();

        m_logFile.open(m_logFilePath, std::ios::out);
        if (m_logFile.is_open())
        {
            m_logFile << "Time_s,Frequency_Hz,Rate5pt_Hz_s,Rate30pt_Hz_s,Rate60pt_Hz_s,OscillatorStatus,SampleNumber,DurationMs,DeviceId\n";
            m_logging = true;
            m_logSavedMessage.clear();

            char fullPath[MAX_PATH];
            GetFullPathNameA(m_logFilePath.c_str(), MAX_PATH, fullPath, nullptr);
            std::cout << "[Channel " << m_id << "] Logging started: " << fullPath << std::endl;
        }
        else
        {
            m_lastError = "Failed to open log file: " + m_logFilePath;
        }
    }
    else
    {
        FlushLog();
        m_logging = false;
        m_logSavedMessage = "Log \"" + m_logFilePath + "\" was saved successfully.";
    }
}

void MonitorChannel::FlushLog()
{
    if (m_logFile.is_open())
    {
        m_logFile.flush();
        m_logFile.close();
    }
}

void MonitorChannel::Update()
{
    if (!m_connected) return;

    Prevac::FrequencyReading r;
    Prevac::FrameDebugInfo d;
    if (m_worker.PollLatest(r, d))
    {
        m_latestDebug = d;
        if (d.crcOk)
        {
            m_sensorConnected = (r.oscillatorStatus == 1);

            if (m_sensorConnected)
            {
                double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_appStart).count();
                m_time_s.push_back(t);
                m_frequency_hz.push_back(r.frequency_hz);
                if (!m_baselineSet && !m_time_s.empty() && m_time_s.back() >= 10.0)
                {
                    double sum = 0.0;
                    int count = 0;
                    for (size_t i = 0; i < m_time_s.size(); ++i)
                    {
                        if (m_time_s[i] <= 10.0)
                        {
                            sum += m_frequency_hz[i];
                            count++;
                        }
                    }
                    if (count > 0)
                    {
                        m_baselineFreq = sum / count;
                        m_baselineSet = true;
                    }
                }
                auto computeRate = [&](int N) -> double {
                    int n = (int)m_frequency_hz.size();
                    if (n <= N) return 0.0;
                    double df = m_frequency_hz[n - 1] - m_frequency_hz[n - 1 - N];
                    double dt = m_time_s[n - 1] - m_time_s[n - 1 - N];
                    return dt != 0.0 ? df / dt : 0.0;
                };
                m_rate5.push_back(computeRate(5));
                m_rate30.push_back(computeRate(30));
                m_rate60.push_back(computeRate(60));

                if (m_logging && m_logFile.is_open())
                {
                    m_logFile << t << "," << r.frequency_hz << "," << m_rate5.back() << ","
                              << m_rate30.back() << "," << m_rate60.back() << ","
                              << (int)r.oscillatorStatus << "," << (int)r.sampleNumber << ","
                              << r.durationMs << "," << (int)r.deviceId << "\n";
                    m_logFile.flush();
                }
            }
            // else: oscillator disconnected -- skip plotting/logging this point entirely
        }
    }
}

void MonitorChannel::Render()
{
    ImGui::PushID(m_id);

    const bool poppedOutThisFrame = m_poppedOut;

    bool windowOpen = true;
    if (poppedOutThisFrame)
    {
        char winTitle[64];
        snprintf(winTitle, sizeof(winTitle), "Channel %d###ChannelWindow_%d", m_id, m_id);
        ImGui::SetNextWindowSize(ImVec2(520, 900), ImGuiCond_FirstUseEver);
        ImGui::Begin(winTitle, &windowOpen);
    }
    else
    {
        ImGui::BeginChild("ChannelPanel", ImVec2(520, 900), true);
    }

    ImGui::Text("Channel %d", m_id);
    ImGui::SameLine();
    if (ImGui::Button(m_poppedOut ? "Dock back" : "Pop out"))
        m_poppedOut = !m_poppedOut;
    ImGui::SameLine();
    if (ImGui::Button("Remove"))
        m_markedForRemoval = true;

    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("COM Port", m_comPortBuf))
    {
        for (const auto& port : m_availablePorts)
        {
            bool isSelected = (port == m_comPortBuf);
            if (ImGui::Selectable(port.c_str(), isSelected))
                strncpy_s(m_comPortBuf, sizeof(m_comPortBuf), port.c_str(), _TRUNCATE);
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        m_availablePorts = ListAvailableComPorts();
    ImGui::SameLine();
    if (!m_connected) { if (ImGui::Button("Connect")) Connect(); }
    else               { if (ImGui::Button("Disconnect")) Disconnect(); }

    ImGui::Text("Status: %s", m_connected ? "connected" : "not connected");
    if (!m_lastError.empty())
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_lastError.c_str());
    if (m_connected)
    {
        if (m_sensorConnected)
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Sensor: OK");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Sensor: DISCONNECTED (no microbalance detected)");
    }
    ImGui::Separator();

    if (ImGui::Button("Reset plot"))
    {
        m_time_s.clear(); m_frequency_hz.clear();
        m_rate5.clear(); m_rate30.clear(); m_rate60.clear();
        m_appStart = std::chrono::steady_clock::now();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_logging ? "Stop and save logging" : "Start logging"))
        ToggleLogging();
    ImGui::SameLine();
    {
        ImVec2 boxSize(20, 20);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 boxColor = m_logging ? IM_COL32(0, 200, 0, 255) : IM_COL32(100, 100, 100, 255);
        dl->AddRectFilled(p, ImVec2(p.x + boxSize.x, p.y + boxSize.y), boxColor, 3.0f);
        dl->AddRect(p, ImVec2(p.x + boxSize.x, p.y + boxSize.y), IM_COL32(0, 0, 0, 255), 3.0f);
        ImGui::Dummy(boxSize);
    }

    if (m_logging)
        ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Logging to: %s", m_logFilePath.c_str());
    else if (!m_logSavedMessage.empty())
        ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "%s", m_logSavedMessage.c_str());

    if (!m_frequency_hz.empty())
        ImGui::Text("Latest: %.2f Hz", m_frequency_hz.back());
    ImGui::Text("Polls: %d  OK: %d", m_worker.GetPollCount(), m_worker.GetSuccessCount());

    ImGui::SeparatorText("Frequency");
    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("+/- Hz window", &m_freqWindowHz, 0.5f, 0.5f, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Set new reference"))
    {
        if (!m_time_s.empty())
        {
            double latestTime = m_time_s.back();
            double sum = 0.0;
            int count = 0;
            for (size_t i = m_time_s.size(); i-- > 0; )
            {
                if (latestTime - m_time_s[i] > 3.0)
                    break; // walking backward, stop once we're older than 3s
                sum += m_frequency_hz[i];
                count++;
            }
            if (count > 0)
            {
                m_baselineFreq = sum / count;
                m_baselineSet = true;
            }
        }
    }
    if (ImPlot::BeginPlot("Frequency", ImVec2(-1, 260)))
    {
        ImPlot::SetupAxes("Time [s]", "Frequency [Hz]", ImPlotAxisFlags_AutoFit, 0);

        if (!m_frequency_hz.empty())
        {
            double center = m_baselineSet ? m_baselineFreq : m_frequency_hz.back();
            ImPlot::SetupAxisLimits(ImAxis_Y1, center - m_freqWindowHz, center + m_freqWindowHz, ImGuiCond_Always);

            ImPlotSpec spec; spec.LineColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f); spec.LineWeight = 2.0f;
            ImPlot::PlotLine("Frequency", m_time_s.data(), m_frequency_hz.data(), (int)m_time_s.size(), spec);
        }
        ImPlot::EndPlot();
    }

    ImGui::SeparatorText("Deposition Rate");
    if (ImPlot::BeginPlot("Deposition Rate", ImVec2(-1, 260)))
    {
        ImPlot::SetupAxes("Time [s]", "Rate [Hz/s]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!m_time_s.empty())
        {
            ImPlotSpec s5; s5.LineColor = ImVec4(1, 0.4f, 0.4f, 1); s5.LineWeight = 1.7f;
            ImPlot::PlotLine("5pt", m_time_s.data(), m_rate5.data(), (int)m_time_s.size(), s5);
            ImPlotSpec s30; s30.LineColor = ImVec4(0.4f, 1, 0.4f, 1); s30.LineWeight = 1.7f;
            ImPlot::PlotLine("30pt", m_time_s.data(), m_rate30.data(), (int)m_time_s.size(), s30);
            ImPlotSpec s60; s60.LineColor = ImVec4(1.0f, 0.95f, 0.3f, 1.0f); s60.LineWeight = 1.7f;
            ImPlot::PlotLine("60pt", m_time_s.data(), m_rate60.data(), (int)m_time_s.size(), s60);
        }
        ImPlot::EndPlot();
    }

    if (poppedOutThisFrame)
    {
        ImGui::End();
        if (!windowOpen) m_poppedOut = false; // clicking the window's X docks it back, doesn't delete the channel
    }
    else
    {
        ImGui::EndChild();
    }

    ImGui::PopID();
}