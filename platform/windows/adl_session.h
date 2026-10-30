#pragma once
#include "../../adl-sdk/include/adl_sdk.h"
#include <windows.h>

// Function signatures follow AMD's public ADL API, not an upstream CLI wrapper.
struct AdlFunctions
{
    int (*create)(ADL_MAIN_MALLOC_CALLBACK, int) = nullptr;
    int (*destroy)() = nullptr;
    int (*adapterCount)(int *) = nullptr;
    int (*adapters)(LPAdapterInfo, int) = nullptr;
    int (*displays)(int, int *, ADLDisplayInfo **, int) = nullptr;
    int (*edid)(int, int, ADLDisplayEDIDData *) = nullptr; // optional identity read
    int (*writeBlock)(int, int, int, int, int, char *, int *, char *) = nullptr;
};
bool InitializeAdl();
void ShutdownAdl();
const AdlFunctions &Adl();
void ReleaseAdlMemory(void **memory);
#ifdef LG_ADL_TESTING
// Available only in the hardware-free backend test executable.
void SetAdlTestLibrary(const wchar_t *path);
#endif
