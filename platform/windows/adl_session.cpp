#include "adl_session.h"
#include <cstdlib>
#include <string>

namespace
{
void *__stdcall AllocateDriverMemory(int bytes)
{
    return bytes > 0 ? std::malloc(static_cast<std::size_t>(bytes)) : nullptr;
}
struct DriverState
{
    HMODULE library = nullptr;
    AdlFunctions functions;
    bool initialized = false;
    void reset()
    {
        if (initialized)
            functions.destroy();
        initialized = false;
        functions = {};
        if (library)
            FreeLibrary(library);
        library = nullptr;
    }
    ~DriverState()
    {
        reset();
    }
};
DriverState &State()
{
    static DriverState state;
    return state;
}
#ifdef LG_ADL_TESTING
std::wstring testLibrary;
#endif
template <typename Function> bool Resolve(HMODULE library, Function &function, const char *name)
{
    function = reinterpret_cast<Function>(GetProcAddress(library, name));
    return function != nullptr;
}
} // namespace

bool InitializeAdl()
{
    auto &state = State();
    if (state.initialized)
        return true;
    // Production loads the installed AMD driver, never a DLL in the app folder.
#ifdef LG_ADL_TESTING
    state.library = LoadLibraryExW(testLibrary.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    state.library = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
    if (!state.library)
        return false;
    auto &api = state.functions;
    const bool complete = Resolve(state.library, api.create, "ADL_Main_Control_Create") &&
                          Resolve(state.library, api.destroy, "ADL_Main_Control_Destroy") &&
                          Resolve(state.library, api.adapterCount, "ADL_Adapter_NumberOfAdapters_Get") &&
                          Resolve(state.library, api.adapters, "ADL_Adapter_AdapterInfo_Get") &&
                          Resolve(state.library, api.displays, "ADL_Display_DisplayInfo_Get") &&
                          Resolve(state.library, api.writeBlock, "ADL_Display_DDCBlockAccess_Get");
    if (complete)
        Resolve(state.library, api.edid, "ADL_Display_EdidData_Get");
    if (complete)
        state.initialized = api.create(AllocateDriverMemory, 1) == ADL_OK;
    if (!state.initialized)
        state.reset();
    return state.initialized;
}
void ShutdownAdl()
{
    State().reset();
}
const AdlFunctions &Adl()
{
    return State().functions;
}
void ReleaseAdlMemory(void **memory)
{
    if (memory)
    {
        std::free(*memory);
        *memory = nullptr;
    }
}
#ifdef LG_ADL_TESTING
void SetAdlTestLibrary(const wchar_t *path)
{
    ShutdownAdl();
    testLibrary = path;
}
#endif
