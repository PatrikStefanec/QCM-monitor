#include "ComPortList.h"
#include <windows.h>
#include <algorithm>

std::vector<std::string> ListAvailableComPorts()
{
    std::vector<std::string> ports;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                       0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char valueName[256];
        char valueData[256];
        DWORD index = 0;

        while (true)
        {
            DWORD valueNameSize = sizeof(valueName);
            DWORD valueDataSize = sizeof(valueData);
            DWORD valueType;

            LONG result = RegEnumValueA(hKey, index, valueName, &valueNameSize,
                                         nullptr, &valueType,
                                         (LPBYTE)valueData, &valueDataSize);
            if (result != ERROR_SUCCESS)
                break;

            ports.emplace_back(valueData, valueDataSize > 0 ? valueDataSize - 1 : 0);
            index++;
        }
        RegCloseKey(hKey);
    }

    std::sort(ports.begin(), ports.end());
    return ports;
}