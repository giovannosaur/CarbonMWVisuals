// dllmain.cpp -- CarbonMWVisuals.asi entry point
//
// Startup order (DLL_PROCESS_ATTACH):
//   1. game folder, .ini path, open CarbonMWVisuals.log, [Main] Enabled (0 = do nothing), [Debug]
//   2. ShaderLoader : hook effect creation -> load fx\*.fx from the game folder
//   3. Lights       : light patches ([Lights] in the .ini)
//   4. Shadows      : dynamic sun shadows + per-frame hook (FrontEnd and TimeOfDay also run from it)
#include "pch.h"
#include "Common.h"

static void Startup(HMODULE hModule)
{
    (void)hModule;
    GetModuleFileNameA(NULL, g_gameDir, MAX_PATH);
    char* slash = strrchr(g_gameDir, '\\');
    if (slash) *slash = 0;
    snprintf(g_iniPath, sizeof(g_iniPath), "%s\\scripts\\CarbonMWVisuals.ini", g_gameDir);

    char logPath[MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%s\\CarbonMWVisuals.log", g_gameDir);
    Log_Open(logPath);
    Log("CarbonMWVisuals %s - game dir: %s\n", CMWV_VERSION, g_gameDir);
    if (!GetPrivateProfileIntA("Main", "Enabled", 1, g_iniPath)) {
        Log("[Main] Enabled=0 -> CarbonMWVisuals is off, nothing is loaded or patched (vanilla Carbon)\n");
        return;
    }
    g_debugMode = GetPrivateProfileIntA("Debug", "DebugMode", 0, g_iniPath);
    if (g_debugMode) Log("Debug mode ON (debug hotkeys + verbose logging)\n");

    if (!Loader_Install()) return;
    Lights_Install();
    Shadow_Install();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    (void)lpReserved;
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Startup(hModule);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
