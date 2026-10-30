#include "../adl-sdk/include/adl_sdk.h"
#include <array>
#include <cstring>

namespace
{
ADL_MAIN_MALLOC_CALLBACK allocate = nullptr;
int createStatus = 0, writeStatus = 0, creates = 0, destroys = 0, writes = 0;
std::array<unsigned char, 8> lastPacket{};
int lastAdapter = -1, lastDisplay = -1;
} // namespace
#define EXPORT extern "C" __declspec(dllexport)
EXPORT void FakeSetStatus(int create, int write)
{
    createStatus = create;
    writeStatus = write;
}
EXPORT int FakeCounter(int index)
{
    return index == 0 ? creates : index == 1 ? destroys : writes;
}
EXPORT const unsigned char *FakePacket()
{
    return lastPacket.data();
}
EXPORT int FakeTarget(int index)
{
    return index == 0 ? lastAdapter : lastDisplay;
}
EXPORT int ADL_Main_Control_Create(ADL_MAIN_MALLOC_CALLBACK callback, int connected)
{
    ++creates;
    allocate = callback;
    return connected == 1 ? createStatus : -99;
}
EXPORT int ADL_Main_Control_Destroy()
{
    ++destroys;
    return 0;
}
EXPORT int ADL_Adapter_NumberOfAdapters_Get(int *count)
{
    *count = 1;
    return 0;
}
EXPORT int ADL_Adapter_AdapterInfo_Get(LPAdapterInfo info, int bytes)
{
    if (bytes < int(sizeof(AdapterInfo)) || info[0].iSize != sizeof(AdapterInfo))
        return -2;
    info[0].iAdapterIndex = 7;
    return 0;
}
EXPORT int ADL_Display_DisplayInfo_Get(int adapter, int *count, ADLDisplayInfo **info, int)
{
    if (adapter != 7 || !allocate)
        return -2;
    *count = 1;
    *info = static_cast<ADLDisplayInfo *>(allocate(sizeof(ADLDisplayInfo)));
    std::memset(*info, 0, sizeof(ADLDisplayInfo));
    (*info)->displayID.iDisplayLogicalIndex = 3;
    return 0;
}
#ifndef LG_FAKE_ADL_INCOMPLETE
EXPORT int ADL_Display_DDCBlockAccess_Get(int adapter, int display, int option, int command, int bytes,
                                          char *data, int *received, char *read)
{
    if (option || command || bytes != 8 || !received || read)
        return -2;
    ++writes;
    lastAdapter = adapter;
    lastDisplay = display;
    std::memcpy(lastPacket.data(), data, 8);
    *received = 0;
    return writeStatus;
}
#endif

EXPORT int ADL_Display_EdidData_Get(int adapter, int display, ADLDisplayEDIDData *data)
{
    if (adapter != 7 || display != 3 || !data || data->iSize != sizeof(*data))
        return -2;
    std::memset(data->cEDIDData, 0, sizeof(data->cEDIDData));
    const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
    std::memcpy(data->cEDIDData, header, sizeof(header));
    data->cEDIDData[127] = 6; // base-block checksum
    data->iEDIDSize = 128;
    return 0;
}
