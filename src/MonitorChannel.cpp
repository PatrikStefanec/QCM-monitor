#include "MonitorChannel.h"
#include "imgui.h"
#include "implot.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

MonitorChannel::MonitorChannel(int id) : m_id(id)
{
    snprintf(m_comPortBuf, sizeof(m_comPortBuf), "COM%d", id + 4);
    m_appStart = std::chrono::steady_clock::now();
    m_availablePorts = ListAvailableComPorts();
}

MonitorChannel::~MonitorChannel()
{
    m_worker.Stop(); // stop the polling thread if still running
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
}

void MonitorChannel::Update(std::ofstream* sharedLog, bool logging, const char* label)
    {
    if (!m_connected) return;

    Prevac::FrequencyReading r;
    Prevac::FrameDebugInfo d;
    if (m_worker.PollLatest(r, d))
    {
        m_latestDebug = d;
        if (d.crcOk)
        {
            m_lastDeviceId = r.deviceId; // remember which device type answered
            m_sensorConnected = (r.oscillatorStatus == 1);

            if (m_sensorConnected)
            {
                double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_appStart).count();
                m_time_s.push_back(t);
                m_frequency_hz.push_back(r.frequency_hz);

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

                if (logging && sharedLog && sharedLog->is_open())
                {
                    *sharedLog << m_id << "," << t << "," << r.frequency_hz << ","
                        << m_rate5.back() << "," << m_rate30.back() << "," << m_rate60.back() << ","
                        << (int)r.oscillatorStatus << "," << (int)r.sampleNumber << ","
                        << r.durationMs << "," << (int)r.deviceId << ","
                        << "\"" << label << "\"" << "\n";
                    sharedLog->flush();
                }
            }
        }
    }
}

void MonitorChannel::PlotSeries(const ImVec4& color, int rateWindow)
{
    if (m_time_s.empty()) return;

    const char* windowNames[] = { "5pt", "30pt", "60pt" };
    const std::vector<double>& rate = (rateWindow == 0) ? m_rate5 : (rateWindow == 1) ? m_rate30 : m_rate60;

    char nameF[32], nameR[48];
    snprintf(nameF, sizeof(nameF), "Ch%d freq", m_id);
    snprintf(nameR, sizeof(nameR), "Ch%d rate (%s)", m_id, windowNames[rateWindow]);

    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    ImPlotSpec sf; sf.LineColor = color; sf.LineWeight = 2.0f;
    ImPlot::PlotLine(nameF, m_time_s.data(), m_frequency_hz.data(), (int)m_time_s.size(), sf);

    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    ImPlotSpec sr; sr.LineColor = color; sr.LineWeight = 1.2f;
    ImPlot::PlotLine(nameR, m_time_s.data(), rate.data(), (int)m_time_s.size(), sr);
}

void MonitorChannel::Render(float width, float height, bool showPlots)
{
    ImGui::PushID(m_id);
    const bool poppedOutThisFrame = m_poppedOut;
    const bool drawPlots = showPlots || poppedOutThisFrame;

    bool windowOpen = true;
    if (poppedOutThisFrame)
    {
        char winTitle[64];
        snprintf(winTitle, sizeof(winTitle), "Channel %d###ChannelWindow_%d", m_id, m_id);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
        ImGui::Begin(winTitle, &windowOpen);
    }
    else
    {
        ImGui::BeginChild("ChannelPanel", ImVec2(width, height), true);
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

    if (m_lastDeviceId == 13 || m_lastDeviceId == 14)
        ImGui::Text("Status: %s (TM%d)", m_connected ? "connected" : "not connected", m_lastDeviceId);
    else
        ImGui::Text("Status: %s", m_connected ? "connected" : "not connected");
    ImGui::SameLine();
    if (!m_lastError.empty())
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", m_lastError.c_str());
    if (m_connected)
    {
        if (m_sensorConnected)
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Sensor: OK");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Sensor: DISCONNECTED (no microbalance detected)");
    }

    if (m_lastDeviceId == 14)
    {
        ImGui::SameLine();
        const char* rateLabels[] = { "10 sps", "4 sps", "2 sps", "1 sps", "0.5 sps" };
        int rateIndex = m_sampleFreqCode - 1;
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("Rate", &rateIndex, rateLabels, IM_ARRAYSIZE(rateLabels)))
            m_sampleFreqCode = (uint8_t)(rateIndex + 1);
    }

    ImGui::Separator();

    if (ImGui::Button("Reset plot"))
    {
        m_time_s.clear(); m_frequency_hz.clear();
        m_rate5.clear(); m_rate30.clear(); m_rate60.clear();
        m_appStart = std::chrono::steady_clock::now();
    }
    if (drawPlots) 
    {
        if (!m_frequency_hz.empty())
        ImGui::Text("Latest: %.2f Hz", m_frequency_hz.back());
        // ImGui::Text("Polls: %d  OK: %d", m_worker.GetPollCount(), m_worker.GetSuccessCount());
        float remaining = ImGui::GetContentRegionAvail().y;
        float overhead = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y * 2; // "Deposition Rate" header + 30pt text line
        float plotH = (std::max)(120.0f, (remaining - overhead) * 0.5f);
        if (ImPlot::BeginPlot("Frequency", ImVec2(-1, plotH)))
        {
            ImPlot::SetupAxes("Time [s]", "Frequency [Hz]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

            if (!m_frequency_hz.empty())
            {
                // double center = m_frequency_hz.back();
                // ImPlot::SetupAxisLimits(ImAxis_Y1, center - m_freqWindowHz, center + m_freqWindowHz, ImGuiCond_Always);

                ImPlotSpec spec; spec.LineColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f); spec.LineWeight = 2.0f;
                ImPlot::PlotLine("Frequency", m_time_s.data(), m_frequency_hz.data(), (int)m_time_s.size(), spec);
            }
            ImPlot::EndPlot();
        }

        ImGui::SeparatorText("Deposition Rate");
        if (!m_time_s.empty())
            ImGui::Text("30pt rate: %.4f Hz/s", m_rate30.data()[m_rate30.size() - 1]);
        if (ImPlot::BeginPlot("Deposition Rate", ImVec2(-1, plotH)))
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