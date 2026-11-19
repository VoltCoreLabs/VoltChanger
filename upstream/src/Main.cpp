#include <windows.h>

#include "Hooks/Hooks.h"
#include "SDK/Logger/Logger.h"
#include "SDK/SDK.h"

static DWORD WINAPI MainThread(LPVOID)
{
    if (!Logger::Init())
        return 0;

    Log("Loaded");
    Log("OVR session pid=%lu", GetCurrentProcessId());

    const char* RequiredModules[] = {
        "client.dll", "engine2.dll", "panorama.dll", "v8.dll",
        "schemasystem.dll", "resourcesystem.dll", "steam_api64.dll"
    };
    bool ModulesReady = false;
    for (unsigned Attempt = 0; Attempt < 480; ++Attempt)
    {
        const char* Missing = nullptr;
        for (const char* Module : RequiredModules)
            if (!GetModuleHandleA(Module)) { Missing = Module; break; }
        if (!Missing) { ModulesReady = true; break; }
        if (Attempt % 20 == 0)
            Log("OVR waiting for module: %s", Missing);
        Sleep(250);
    }
    if (!ModulesReady)
    {
        LogE("OVR initialization failed: game modules not ready after 120 seconds");
        return 0;
    }

    if (!SDK::Init())
    {
        LogE("OVR initialization failed: SDK init failed");
        return 0;
    }

    if (Hooks::Install())
        Log("OVR initialization complete");
    else
        LogE("OVR initialization incomplete: one or more hooks failed");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        if (HANDLE Thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
            CloseHandle(Thread);
    }
    else if (Reason == DLL_PROCESS_DETACH)
        LogW("Detach, process exit %d", Reserved != nullptr);
    return TRUE;
}
