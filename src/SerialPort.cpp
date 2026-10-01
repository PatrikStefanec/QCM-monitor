#include "SerialPort.h"

bool SerialPort::Open(const std::string& portName, DWORD baudRate)
{
    Close();

    std::string fullName = "\\\\.\\" + portName;
    
    m_handle = CreateFileA(fullName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    
    if (m_handle == INVALID_HANDLE_VALUE)
        return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(m_handle, &dcb))
    {
        Close();
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;

    if (!SetCommState(m_handle, &dcb))
    {
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 50;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(m_handle, &timeouts);

    // Flush any stale bytes sitting in the buffers
    PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return true;
}

void SerialPort::Close()
{
    if (m_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

int SerialPort::Write(const uint8_t* data, size_t len)
{
    if (!IsOpen()) return -1;
    DWORD written = 0;
    if (!WriteFile(m_handle, data, (DWORD)len, &written, nullptr))
        return -1;
    return (int)written;
}

int SerialPort::Read(uint8_t* buffer, size_t maxLen)
{
    if (!IsOpen()) return -1;
    DWORD read = 0;
    if (!ReadFile(m_handle, buffer, (DWORD)maxLen, &read, nullptr))
        return -1;
    return (int)read;
}