// ShaderLoader.cpp  --  .fx shader loader of CarbonMWVisuals for NFS Carbon v1.4 (NFSC.exe)
//
// Why not ExtendedCustomization's loader? EC compiles/creates effects with d3dx9_43,
// while Carbon's effect pool is created by d3dx9_30. "shared" parameters of a d3dx9_43
// effect do not link properly to a d3dx9_30 pool -> WorldViewProj etc. break.
// This ASI compiles fx\<name>.fx with the game's own d3dx9_30 into the same pool.
//
// Hook: the call to D3DXCreateEffectFromResourceA at 0x72B6D4 (the only call site).
// At the call: edx = effect_index * 0x90 -> efx path at [0xA6408C + edx]
// ("fx\world.efx" -> "fx\world.fx"). If the .fx file is missing or fails to compile,
// the original target is called (exe resource, or EC's hook if EC patched it first)
// with edx restored.
#include "pch.h"
#include "Common.h"

#define CALL_SITE      0x0072B6D4u
#define ORIG_TARGET    0x0086B2CCu   // thunk: jmp [D3DXCreateEffectFromResourceA]
#define EFX_PATH_TABLE 0x00A6408Cu   // char* efxPath in the effect table (stride 0x90, edx is already *0x90)

typedef HRESULT (WINAPI *PFN_CreateFromFile)(void* dev, const char* file, const void* defines,
        void* include, DWORD flags, void* pool, void** ppEffect, void** ppErrors);

DWORD g_savedEdx  ASMNAME("g_savedEdx")  = 0;
DWORD g_origTarget ASMNAME("g_origTarget") = ORIG_TARGET;
static PFN_CreateFromFile g_createFromFile = NULL;

//-----------------------------------------------------------------------------
// Car ambient shadow / Extended Customization neon.
// CarRenderInfo::DrawAmbientShadow draws its quads with WorldPrelitShader; EC draws the neon
// glow through the same function with the neon colour in the vertex colour. The MW daytime
// worldprelit.fx replaces the vertex colour with MW light (baked night lighting), so the neon
// turned white. The two texture coordinate calls of that function (0x7BEB07 / 0x7BEB27, both
// "call 0x70F5D0" = set u, v) go through Prelit_UVThunk, which adds PRELIT_UV_TAG (4096) to u;
// worldprelit.fx recognises it, removes it again and keeps Carbon's shading for those quads.
// Only active when the loaded worldprelit.fx knows the tag (g_prelitTag).
//-----------------------------------------------------------------------------
#define PRELIT_FN_SET_UV   0x0070F5D0u
static const DWORD kPrelitUVCalls[2] = { 0x007BEB07u, 0x007BEB27u };
int   g_prelitTag  ASMNAME("g_prelitTag")  = 0;
float g_prelitTagU ASMNAME("g_prelitTagU") = 4096.0f;   // = PRELIT_UV_TAG in worldprelit.fx

#ifdef CMWV_MSASM
NAKED void Prelit_UVThunk(void)
{
    __asm {
        cmp dword ptr [g_prelitTag], 0
        je pu_go
        fld dword ptr [esp + 4]
        fadd dword ptr [g_prelitTagU]
        fstp dword ptr [esp + 4]
    pu_go:
        push 0x0070F5D0
        ret
    }
}
#else
void Prelit_UVThunk(void) ASMNAME("Prelit_UVThunk");
__asm__(
    ".text\n"
    ".globl Prelit_UVThunk\n"
    "Prelit_UVThunk:\n"                     // called instead of 0x70F5D0(float u, float v)
    "    cmpl $0, g_prelitTag\n"
    "    je 1f\n"
    "    flds 4(%esp)\n"
    "    fadds g_prelitTagU\n"
    "    fstps 4(%esp)\n"
    "1:  pushl $0x0070F5D0\n"
    "    ret\n"
);
#endif

static void Prelit_PatchUV(void)
{
    int n = 0;
    for (int k = 0; k < 2; k++) {
        BYTE* p = (BYTE*)kPrelitUVCalls[k];
        if (p[0] != 0xE8 || kPrelitUVCalls[k] + 5 + *(LONG*)(p + 1) != PRELIT_FN_SET_UV) continue;
        DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(LONG*)(p + 1) = (LONG)((DWORD)Prelit_UVThunk - (kPrelitUVCalls[k] + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        n++;
    }
    if (n == 2) Log("prelit: car ambient shadow / neon keep Carbon's shading (vertex colour)\n");
    else Log("WARNING: car ambient shadow UV calls do not match (%d/2 patched): EC neon may look white\n", n);
}

static bool FileContains(const char* path, const char* text)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    static char buf[256 * 1024];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;
    return strstr(buf, text) != NULL;
}

// Call the original target with the same stack arguments and edx restored.
HRESULT WINAPI CallOriginal(void* dev, HMODULE mod, const char* res, const void* defines,
        void* include, DWORD flags, void* pool, void** ppEffect, void** ppErrors) ASMNAME("CallOriginal");
#ifdef CMWV_MSASM
NAKED HRESULT WINAPI CallOriginal(void* dev, HMODULE mod, const char* res, const void* defines,
        void* include, DWORD flags, void* pool, void** ppEffect, void** ppErrors)
{
    __asm {
        mov edx, g_savedEdx
        jmp dword ptr [g_origTarget]
    }
}
#else
__asm__(
    ".text\n"
    ".globl CallOriginal\n"
    "CallOriginal:\n"
    "    movl g_savedEdx, %edx\n"
    "    jmp *g_origTarget\n"
);
#endif

HRESULT WINAPI HookImpl(void* dev, HMODULE mod, const char* res, const void* defines,
        void* include, DWORD flags, void* pool, void** ppEffect, void** ppErrors) ASMNAME("HookImpl");

// Thunk called from 0x72B6D4: save edx (effect index), then go to HookImpl.
#ifdef CMWV_MSASM
NAKED void HookThunk(void)
{
    __asm {
        mov g_savedEdx, edx
        jmp HookImpl
    }
}
#else
void HookThunk(void) ASMNAME("HookThunk");
__asm__(
    ".text\n"
    ".globl HookThunk\n"
    "HookThunk:\n"
    "    movl %edx, g_savedEdx\n"
    "    jmp HookImpl\n"
);
#endif

HRESULT WINAPI HookImpl(void* dev, HMODULE mod, const char* res, const void* defines,
        void* include, DWORD flags, void* pool, void** ppEffect, void** ppErrors)
{
    DWORD edxv = g_savedEdx;
    const char* efx = NULL;
    if (edxv < 0x90u * 64u && (edxv % 0x90u) == 0)
        efx = *(const char**)(EFX_PATH_TABLE + edxv);

    char fx[MAX_PATH] = {0};
    if (efx && g_createFromFile) {
        snprintf(fx, sizeof(fx), "%s\\%s", g_gameDir, efx);
        char* dot = strrchr(fx, '.');
        if (dot) strcpy(dot, ".fx");
        if (GetFileAttributesA(fx) != INVALID_FILE_ATTRIBUTES) {
            void* errs = NULL;
            HRESULT hr = g_createFromFile(dev, fx, defines, include, flags, pool, ppEffect, &errs);
            if (SUCCEEDED(hr)) {
                Log("[OK]    %-30s <- %s (d3dx9_30, pool %p)\n", res, fx, pool);
                Shadow_OnEffect(res, ppEffect ? *ppEffect : NULL);
                if (res && strcmp(res, "IDI_WORLDPRELIT_FX") == 0)
                    g_prelitTag = FileContains(fx, "PRELIT_UV_TAG") ? 1 : 0;
                if (errs) { Log("        warnings:\n%s\n", BufferText(errs)); ReleaseCom(errs); }
                if (ppErrors) *ppErrors = NULL;
                return hr;
            }
            Log("[FAILED] %-30s <- %s  HRESULT 0x%08lX -> using the original shader\n%s\n",
                res, fx, (unsigned long)hr, BufferText(errs));
            ReleaseCom(errs);
        }
    }
    if (res && strcmp(res, "IDI_WORLDPRELIT_FX") == 0) g_prelitTag = 0;   // not our shader: no tag
    HRESULT hr = CallOriginal(dev, mod, res, defines, include, flags, pool, ppEffect, ppErrors);
    Log("[orig]  %-30s %s\n", res ? res : "?", SUCCEEDED(hr) ? "" : "(FAILED!)");
    return hr;
}

static bool Loader_Hook(void)
{
    BYTE* site = (BYTE*)CALL_SITE;
    if (site[0] != 0xE8) {
        Log("ERROR: byte at 0x%08X is not a CALL (0x%02X). Different NFSC.exe version? The ASI does nothing.\n",
            CALL_SITE, site[0]);
        return false;
    }
    DWORD target = CALL_SITE + 5 + *(LONG*)(site + 1);
    g_origTarget = target;
    if (target != ORIG_TARGET)
        Log("Info: call site already hooked by another ASI (target 0x%08lX, probably ExtendedCustomization). "
            "Effects without a .fx file are passed on to it.\n", (unsigned long)target);

    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    *(LONG*)(site + 1) = (LONG)((DWORD)HookThunk - (CALL_SITE + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("Hook installed at 0x%08X\n", CALL_SITE);
    return true;
}

// Find D3DXCreateEffectFromFileA in d3dx9_30 (the version the game uses) and install the hook.
bool Loader_Install(void)
{
    // Make sure this is NFSC.exe v1.4: the effect table must start with "fx\world.efx".
    const char* first = *(const char**)EFX_PATH_TABLE;
    if (IsBadReadPtr(first, 4) || strcmp(first, "fx\\world.efx") != 0) {
        Log("ERROR: effect table does not match (not NFSC.exe v1.4?). The ASI does nothing.\n");
        return false;
    }
    HMODULE d3dx = GetModuleHandleA("d3dx9_30.dll");
    if (!d3dx) d3dx = LoadLibraryA("d3dx9_30.dll");
    if (d3dx) g_createFromFile = (PFN_CreateFromFile)GetProcAddress(d3dx, "D3DXCreateEffectFromFileA");
    Log("d3dx9_30: %p, D3DXCreateEffectFromFileA: %p\n", (void*)d3dx, (void*)g_createFromFile);
    if (!g_createFromFile) { Log("ERROR: d3dx9_30 not found.\n"); return false; }
    // Without the shader hook nothing else is installed either (no half-applied look).
    if (!Loader_Hook()) return false;
    Prelit_PatchUV();
    return true;
}
