// Common.h -- shared declarations for all CarbonMWVisuals modules
#pragma once
#include "pch.h"

#define CMWV_VERSION "1.0"

// ---------------------------------------------------------------------------------------------
// Compiler compatibility: MSVC (Visual Studio, x86/Win32) and MinGW GCC (i686-w64-mingw32-g++).
// Thunks/code caves are written twice: __declspec(naked) + __asm { } (MSVC) and AT&T asm (GCC).
// ---------------------------------------------------------------------------------------------
#if defined(_MSC_VER) || defined(CMWV_MSVC_ASM)
  #define CMWV_MSASM 1
  #define ASMNAME(x)
  #define NAKED __declspec(naked)
  #define THISCALL __thiscall
#else
  #define ASMNAME(x) __asm__(x)
  #define THISCALL __attribute__((thiscall))
#endif
#if defined(_MSC_VER) && !defined(_M_IX86)
  #error "CarbonMWVisuals must be built as Win32 (x86): NFSC.exe is a 32-bit game."
#endif

// --- utilities (Common.cpp) ----------------------------------------------------------------
extern char  g_gameDir[MAX_PATH];     // game folder (where NFSC.exe lives)
extern char  g_iniPath[MAX_PATH];     // <game>\scripts\CarbonMWVisuals.ini
extern int   g_debugMode;             // [Debug] DebugMode: enables debug hotkeys and verbose logging
void        Log(const char* fmt, ...);
bool        Log_Open(const char* path);
const char* BufferText(void* buf);    // ID3DXBuffer -> text
void        ReleaseCom(void* obj);
int         KeyPressed(int vk);       // returns 1 once per key press (0 if vk == 0)
int         ParseKey(const char* s);  // "F3", "A", "0x72", "114", "NONE" -> virtual-key code (0 = disabled)
int         Cfg_Key(const char* section, const char* key, const char* def);   // read a hotkey from the .ini
int         PatchCall(DWORD site, DWORD expectTarget, void* fn);              // redirect a rel32 CALL

// --- ID3DXEffect through its vtable --------------------------------------------------------
typedef void* (WINAPI *FX_GetParamByName)(void*, void*, const char*);
typedef HRESULT (WINAPI *FX_SetMatrix)(void*, void*, const float*);
typedef HRESULT (WINAPI *FX_SetVector)(void*, void*, const float*);
typedef HRESULT (WINAPI *FX_GetVector)(void*, void*, float*);
typedef HRESULT (WINAPI *FX_SetTexture)(void*, void*, void*);
typedef HRESULT (WINAPI *FX_SetInt)(void*, void*, int);
typedef HRESULT (WINAPI *FX_SetFloat)(void*, void*, float);
typedef HRESULT (WINAPI *FX_GetFloat)(void*, void*, float*);
inline void* FxParam(void* fx, const char* n) { return fx ? ((FX_GetParamByName)(*(void***)fx)[9])(fx, NULL, n) : NULL; }
inline void FxSetMatrix(void* fx, const char* n, const float* m) { void* h = FxParam(fx, n); if (h) ((FX_SetMatrix)(*(void***)fx)[38])(fx, h, m); }
inline void FxSetVector(void* fx, const char* n, const float* v) { void* h = FxParam(fx, n); if (h) ((FX_SetVector)(*(void***)fx)[34])(fx, h, v); }
inline int  FxGetVector(void* fx, const char* n, float* v) { void* h = FxParam(fx, n); return h && SUCCEEDED(((FX_GetVector)(*(void***)fx)[35])(fx, h, v)); }
inline void FxSetTexture(void* fx, const char* n, void* t) { void* h = FxParam(fx, n); if (h) ((FX_SetTexture)(*(void***)fx)[52])(fx, h, t); }
inline void FxSetInt(void* fx, const char* n, int v) { void* h = FxParam(fx, n); if (h) ((FX_SetInt)(*(void***)fx)[26])(fx, h, v); }
inline void FxSetFloat(void* fx, const char* n, float v) { void* h = FxParam(fx, n); if (h) ((FX_SetFloat)(*(void***)fx)[30])(fx, h, v); }

// --- modules -------------------------------------------------------------------------------
// ShaderLoader.cpp
bool Loader_Install(void);                                  // hooks D3DXCreateEffectFromResourceA
// Shadows.cpp
void Shadow_Install(void);
void Shadow_OnEffect(const char* res, void* effect);        // called by the loader for every port effect
void Shadow_GetSettings(int* on, int* size, int* sizeActive, int* available);
void Shadow_SetSettings(int on, int size);                  // Enable: runtime; Size: from the next start
void Shadow_SaveIni(void);                                  // [Shadows] Enable / Size
void Shadow_ReloadIni(void);
// FrontEnd.cpp
void FE_Init(const char* ini);
void FE_OnEffect(const char* res, void* fx);
void FE_Update(void);
void FE_EnvUpdate(void);
void FE_Reload(void);
int  FE_Generation(void);                                     // bumped when the FE settings change
int  FE_ValueFor(void* fx, const char* param, float out[4]);  // FE value of a parameter (1 = set by the FE)
void FE_GetEnv(int* on, char* set, int setSize);              // MW FE reflection cubemap
void FE_SetEnv(int on, const char* set);
void FE_SaveEnv(void);
// Lights.cpp
void Lights_Install(void);
void Lights_ReadWorldFlares(const char* ini);
void Lights_DebugUpdate(void);
void Lights_SetNight(float night);                          // 0 = day, 1 = night (time of day)
void Lights_SetHeadlights(int on);                          // car headlight beams on/off (time of day)
// TimeOfDay.cpp
void TOD_Init(void);
void TOD_ReadIni(int resetTime);
void TOD_OnEffect(const char* res, void* fx);
void TOD_Update(void);                                       // per frame, before FE_Update
void TOD_UpdatePost(void);                                   // per frame, after FE_Update
extern float g_todShadowScale;                               // sun shadow strength multiplier (0 at night)
struct TodSettings {
    int   enable, mode, paused;
    float hour, startHour, hourLength, nightHourLength, minHour, maxHour, overcast, sunsetHaze;
    float noonElevation, noonAzimuth;                        // degrees
    float headlightsOn, headlightsOff;                       // hours
    float sunElevation, night, lowSun, shadows; int inGame, ready;   // read-only status
};
void TOD_GetSettings(int which, TodSettings* out);          // which: 0 = in-game, 1 = FE
void TOD_SetSettings(int which, const TodSettings* in);
void TOD_SaveIni(void);
void TOD_ReloadIni(void);
int  TOD_GetSync(void);                                     // [Main] SyncTime: in-game and FE share the time
void TOD_SetSync(int on);
// Menu.cpp
void Menu_Init(void);
void Menu_Frame(void);                                       // per frame: installs the Present/Reset hooks
