#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include "SerialPort.h"

namespace Prevac
{
    constexpr uint8_t HEADER = 0xAA;

    // Known constants from the manual
    constexpr uint8_t DEVICE_GROUP_TM13_TM14 = 0xA1;
    constexpr uint8_t DEFAULT_DEVICE_ADDRESS = 0xC8;
    constexpr uint8_t DEFAULT_LOGIC_GROUP    = 0xC8;
    constexpr uint8_t DRIVER_ADDRESS_SELF    = 0x01; // "us", the PC

    constexpr uint8_t FUNC_READ_WRITE_PARAMS = 0x53;
    constexpr uint8_t FUNC_SET_DEVICE_ADDR   = 0x58;
    constexpr uint8_t FUNC_SET_LOGIC_GROUP   = 0x59;
    constexpr uint8_t FUNC_READ_PRODUCT_NUM  = 0xFD;
    constexpr uint8_t FUNC_READ_SERIAL_NUM   = 0xFE;

    // Result of a successful 0x53 query
    struct FrequencyReading
    {
        uint32_t frequency_raw;   // in units of 0.01 Hz
        double   frequency_hz;    // converted
        uint8_t  oscillatorStatus;
        uint8_t  sampleNumber;
        uint16_t durationMs;
        uint8_t  deviceId;        // 13 or 14
    };

    // Debug info about the most recent frame attempt, for troubleshooting
    struct FrameDebugInfo
    {
        bool     gotHeader = false;
        bool     timedOut = false;
        bool     crcOk = false;
        uint8_t  expectedCrc = 0;
        uint8_t  actualCrc = 0;
        std::vector<uint8_t> rawBytes;
    };

    // Builds a full frame (header..CRC) ready to send over the wire.
    std::vector<uint8_t> BuildFrame(uint8_t deviceAddress,
                                     uint8_t deviceGroup,
                                     uint8_t logicGroup,
                                     uint8_t driverAddress,
                                     uint8_t functionCode,
                                     const std::vector<uint8_t>& data);

    // Sends a 0x53 read/write-parameters request and parses the response.
    // sampleFreqCode: 1=10sps, 2=4sps, 3=2sps, 4=1sps, 5=0.5sps (TM14 only; TM13 likely ignores it)
    // Returns std::nullopt on timeout / bad CRC / malformed response.
    std::optional<FrequencyReading> ReadFrequency(SerialPort& port,
                                                    uint8_t deviceAddress,
                                                    uint8_t sampleFreqCode,
                                                    FrameDebugInfo* debugOut = nullptr);}