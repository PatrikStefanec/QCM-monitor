#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort() { Close(); }

    // portName example: "COM5" (for ports > COM9 use "\\\\.\\COM12")
    bool Open(const std::string& portName, DWORD baudRate = 57600);
    void Close();
    bool IsOpen() const { return m_handle != INVALID_HANDLE_VALUE; }

    // Returns number of bytes written, or -1 on error
    int Write(const uint8_t* data, size_t len);
    // Reads up to maxLen bytes, non-blocking-ish (uses short timeout).
    // Returns number of bytes actually read (0 if none available), or -1 on error.
    int Read(uint8_t* buffer, size_t maxLen);

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};
