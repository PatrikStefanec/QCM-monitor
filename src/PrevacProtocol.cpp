#include "PrevacProtocol.h"
#include <chrono>
#include <thread>

namespace Prevac
{
    static uint8_t ComputeCRC(const std::vector<uint8_t>& bytesAfterHeader)
    {
        uint32_t sum = 0;
        for (uint8_t b : bytesAfterHeader)
            sum += b;
        return (uint8_t)(sum & 0xFF);
    }

    std::vector<uint8_t> BuildFrame(uint8_t deviceAddress,
                                     uint8_t deviceGroup,
                                     uint8_t logicGroup,
                                     uint8_t driverAddress,
                                     uint8_t functionCode,
                                     const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> afterHeader;
        afterHeader.push_back((uint8_t)data.size());
        afterHeader.push_back(deviceAddress);
        afterHeader.push_back(deviceGroup);
        afterHeader.push_back(logicGroup);
        afterHeader.push_back(driverAddress);
        afterHeader.push_back(functionCode);
        afterHeader.insert(afterHeader.end(), data.begin(), data.end());

        uint8_t crc = ComputeCRC(afterHeader);

        std::vector<uint8_t> frame;
        frame.push_back(HEADER);
        frame.insert(frame.end(), afterHeader.begin(), afterHeader.end());
        frame.push_back(crc);
        return frame;
    }

    // Reads one full frame from the port: header, length byte, then
    // (5 + length + 1) more bytes (addr/group/group/driver/func + data + CRC).
    // Uses a simple polling loop with a timeout since SerialPort::Read is short-timeout.
    static std::optional<std::vector<uint8_t>> ReadFrame(SerialPort& port, FrameDebugInfo* dbg, int timeoutMs = 300)
    {
        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> buf;
        uint8_t byte;

        while (true)
        {
            if (port.Read(&byte, 1) == 1)
            {
                if (byte == HEADER) { buf.push_back(byte); if (dbg) dbg->gotHeader = true; break; }
            }
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
            {
                if (dbg) dbg->timedOut = true;
                return std::nullopt;
            }
        }

        while (buf.size() < 2)
        {
            if (port.Read(&byte, 1) == 1) buf.push_back(byte);
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
            {
                if (dbg) { dbg->timedOut = true; dbg->rawBytes = buf; }
                return std::nullopt;
            }
        }
        uint8_t dataLen = buf[1];
        size_t totalExpected = 2 + 5 + dataLen + 1;

        while (buf.size() < totalExpected)
        {
            if (port.Read(&byte, 1) == 1) buf.push_back(byte);
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs))
            {
                if (dbg) { dbg->timedOut = true; dbg->rawBytes = buf; }
                return std::nullopt;
            }
        }

        std::vector<uint8_t> afterHeader(buf.begin() + 1, buf.end() - 1);
        uint8_t expectedCrc = ComputeCRC(afterHeader);
        uint8_t actualCrc = buf.back();

        if (dbg)
        {
            dbg->rawBytes = buf;
            dbg->expectedCrc = expectedCrc;
            dbg->actualCrc = actualCrc;
            dbg->crcOk = (expectedCrc == actualCrc);
        }

        if (expectedCrc != actualCrc)
            return std::nullopt;

        return buf;
    }

    std::optional<FrequencyReading> ReadFrequency(SerialPort& port,
                                                    uint8_t deviceAddress,
                                                    uint8_t sampleFreqCode,
                                                    FrameDebugInfo* debugOut)
    {
        std::vector<uint8_t> data = { sampleFreqCode, 0x00, 0x00, 0x00 };
        auto frame = BuildFrame(deviceAddress, DEVICE_GROUP_TM13_TM14,
                                 DEFAULT_LOGIC_GROUP, DRIVER_ADDRESS_SELF,
                                 FUNC_READ_WRITE_PARAMS, data);

        if (port.Write(frame.data(), frame.size()) != (int)frame.size())
            return std::nullopt;

        auto response = ReadFrame(port, debugOut);
        if (!response) return std::nullopt;

        const auto& r = *response;
        uint8_t dataLen = r[1];
        if (dataLen < 9) return std::nullopt;

        const uint8_t* d = &r[7];

        FrequencyReading result{};
        result.frequency_raw = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                                ((uint32_t)d[2] << 8)  |  (uint32_t)d[3];
        result.frequency_hz     = result.frequency_raw * 0.01;
        result.oscillatorStatus = d[4];
        result.sampleNumber     = d[5];
        result.durationMs       = ((uint16_t)d[6] << 8) | d[7];
        result.deviceId         = d[8];

        return result;
    }
}