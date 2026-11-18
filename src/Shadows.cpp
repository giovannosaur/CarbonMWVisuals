// Shadows.cpp -- MW-style dynamic sun shadows (Carbon's shadow map that EA disabled, revived)
//                + the per-frame hook (eDisplayFrame), which also runs the FrontEnd module and debug hotkeys
#include "pch.h"
#include "Common.h"

//=============================================================================
// MW-STYLE DYNAMIC SUN SHADOWS
//
// Carbon v1.4 still contains MW's shadow map system, but it is switched off:
//  - 0x72C9F0 RenderShadowMap(View* src, GrandSceneryCullInfo*, int player)
//    = straight port of MW 0x6E44C0 (camera from the sun direction, fitted 50 m in front
//    of the camera, renders scenery + cars into EVIEW_SHADOWMAP1). Never called.
//  - The 1024x1024 RT (depth D24S8 [NV] / DF16 [ATI]) is created when g_ShadowDetail > 0.
//  - Scenery culling has a case for views 16/17 (flag 0x8100), but the shadow view is never
//    registered with GrandSceneryCullInfo.
//  - Carbon's world shaders never read the shadow map; SHADOWTRANSFORM is never set.
// This module: forces g_ShadowDetail, registers the shadow view for culling, calls
// RenderShadowMap every frame (sun direction = world.fx mwSunDirection), then feeds
// mwShadowMap / mwShadowTransform / mwShadowParams (shared, pool) to the port shaders.
//=============================================================================
#define SH_ADDR_CULL_END     0x007104DFu  // 8B CF 5F E9 -> DoCulling (end of culling setup)
#define SH_ADDR_CALL_DISPLAY 0x0072EE3Cu  // E8 -> eDisplayFrame (main view render)
#define SH_ADDR_CALL_CLAMP   0x00730BB8u  // E8 -> graphics option clamp at init
#define SH_FN_RENDER_SHADOW  0x0072C9F0u
#define SH_FN_CULL_ADD       0x0079AE80u
#define SH_FN_DO_CULLING     0x007A4BA0u
#define SH_FN_DISPLAY_FRAME  0x0072E5B0u
#define SH_FN_CLAMP          0x007112F0u
#define SH_VIEW_SHADOW1      0x00B4B710u  // eView[16] (stride 0x80, base 0xB4AF10)
#define SH_G_DETAIL          0x00A65398u  // g_ShadowDetail
#define SH_G_DETAIL_MAX      0x00A65400u
#define SH_G_HWMODE          0x00AB0AF0u  // 0 = none, 1 = D24S8 compare, 2 = DF16
#define SH_G_DEPTHTEX        0x00AB0C0Cu
#define SH_G_DEPTHSURF       0x00AB0C08u
#define SH_G_COLORSURF       0x00AB0C00u
#define SH_G_GAMEMODE        0x00A99BBCu  // 6 = in-game
#define SH_G_CULL_ON         0x00A63E0Cu
#define SH_G_SUNINFO         0x00B77F34u  // ptr, +0x160 = direction TO the sun
#define SH_G_D3D             0x00AB0AB8u
#define SH_G_ADAPTERFMT      0x00AB0AFCu

typedef void (__cdecl *PFN_RenderShadow)(void* view, void* cull, int player);
typedef void (__cdecl *PFN_DisplayFrame)(void* view, void* cull);
typedef void (__cdecl *PFN_Void)(void);
typedef void (THISCALL *PFN_CullAdd)(void* cull, void* view, int flags);

static int   g_shEnable = 1, g_shDetail = 2, g_shForceHW = 1, g_shLogFrames = 0, g_shFormat = 0, g_shCasters = 0;
static int   g_shSize = 2048;               // shadow map size in use (Carbon original: 1024)
static int   g_shOn = 1;                    // [Shadows] Enable: runtime switch (menu "Enable shadows")
static int   g_shSizeIni = 2048;            // [Shadows] Size as chosen in the menu (used from the next start)
static float g_shStrength = 0.85f;          // shadow strength (0..1)
static float g_shDarkness = 0.4f;           // how much ambient light is also darkened in shadow (0..1)
static float g_shSoftness = 1.0f;           // PCF radius (texels)
static float g_shRange = 90.0f;             // half width of the shadowed area (m)
static float g_shForward = 0.4f;            // shift the area centre ahead of the camera (x Range)
static int   g_shSunSet = 0;                // [Sun] Direction given -> set on every port effect
static float g_shSun[4] = { 0.35f, 0.45f, 0.82f, 0.0f };
static void* g_shAllFx[32]; static int g_shNumFx = 0;
// fake camera for RenderShadowMap: +0x40 = centre of the shadowed area (see Shadow_CamCave)
float g_shFakeCam[32] ASMNAME("g_shFakeCam");
static void* g_shFx = NULL;         // a port effect with the shadow parameters (to set the shared ones)
static void* g_shFxWorld = NULL;    // world.fx: source of mwSunDirection
static void* g_carFx = NULL;        // car.fx: for logging paint material values (diagnostics)
static int   g_shReady = 0, g_shFrames = 0, g_shCullAdds = 0;
DWORD g_shDoCulling ASMNAME("g_shDoCulling") = SH_FN_DO_CULLING;


void Shadow_OnEffect(const char* res, void* effect)
{
    if (!effect) return;
    FE_OnEffect(res, effect);
    // Shader-side debug views are only allowed in debug mode.
    if (!g_debugMode && FxParam(effect, "mwCarGlassDebug")) FxSetFloat(effect, "mwCarGlassDebug", 0.0f);
    // AddRef: the game may recreate effects; the old pointer stays valid until replaced.
    typedef ULONG (WINAPI *AddRefFn)(void*);
    if (FxParam(effect, "mwShadowTransform")) {
        ((AddRefFn)(*(void***)effect)[1])(effect);
        ReleaseCom(g_shFx);
        g_shFx = effect;
        if (g_debugMode) Log("        shadow: effect has mwShadowTransform (%s)\n", res);
    }
    if (FxParam(effect, "mwSunDirection") && g_shNumFx < 32) {
        typedef ULONG (WINAPI *AddRefFn2)(void*);
        ((AddRefFn2)(*(void***)effect)[1])(effect);
        g_shAllFx[g_shNumFx++] = effect;
        if (g_shSunSet) FxSetVector(effect, "mwSunDirection", g_shSun);
    }
    if (res && !strcmp(res, "IDI_CAR_FX")) {
        typedef ULONG (WINAPI *AddRefFn3)(void*);
        ((AddRefFn3)(*(void***)effect)[1])(effect);
        ReleaseCom(g_carFx);
        g_carFx = effect;
    }
    if (res && !strcmp(res, "IDI_WORLD_FX")) {
        ((AddRefFn)(*(void***)effect)[1])(effect);
        ReleaseCom(g_shFxWorld);
        g_shFxWorld = effect;
    }
    TOD_OnEffect(res, effect);   // after mwSunDirection got its [Sun] value: that is the noon sun
}

static int g_shDebug = 0, g_shUser = 1;   // debug view mode / shadows on (debug hotkeys)
static int g_keyReload = 0, g_keyShadowToggle = 0, g_keyShadowView = 0, g_keyDiag = 0;   // [Debug] hotkeys

static void Shadow_SetParams(int on)
{
    if (!g_shFx) return;
    int mode = *(int*)SH_G_HWMODE;
    float p[4] = { on ? g_shStrength * g_todShadowScale : 0.0f, (float)mode, 1.0f / (float)g_shSize, on ? (float)g_shDebug : 0.0f };
    FxSetVector(g_shFx, "mwShadowParams", p);
    float p2[4] = { g_shDarkness, g_shSoftness, 0.0f, 0.0f };
    FxSetVector(g_shFx, "mwShadowParams2", p2);
    FxSetInt(g_shFx, "mwShadowFilter", mode == 1 ? 2 /*LINEAR: PCF hw 2x2*/ : 1 /*POINT*/);
}

// Called at the end of the culling setup (edi = GrandSceneryCullInfo*)
void __cdecl Shadow_AddCull(void* cull) ASMNAME("Shadow_AddCull");
void __cdecl Shadow_AddCull(void* cull)
{
    if (!g_shEnable || !g_shOn || *(int*)SH_G_DETAIL <= 0 || !*(int*)SH_G_HWMODE) return;
    if (!*(int*)SH_G_CULL_ON || *(int*)SH_G_GAMEMODE != 6) return;
    BYTE* view = (BYTE*)SH_VIEW_SHADOW1;
    BYTE* rt = *(BYTE**)(view + 0x50);
    if (!view[0xc] || !rt || !*(DWORD*)(rt + 0x10) || !*(void**)SH_G_DEPTHTEX) return;
    if (*(int*)((BYTE*)cull + 0x3c0) >= 12) return;   // capacity: 12 views (0x50 each)
    ((PFN_CullAdd)SH_FN_CULL_ADD)(cull, view, 0);
    g_shCullAdds++;
}

#ifdef CMWV_MSASM
NAKED void Shadow_CullEndThunk(void)
{
    __asm {
        pushad
        push edi
        call Shadow_AddCull
        add esp, 4
        popad
        mov ecx, edi                    // overwritten original instructions: mov ecx, edi
        pop edi                         //                                     pop edi
        jmp dword ptr [g_shDoCulling]   //                                     jmp DoCulling
    }
}
#else
void Shadow_CullEndThunk(void) ASMNAME("Shadow_CullEndThunk");
__asm__(
    ".text\n"
    ".globl Shadow_CullEndThunk\n"
    "Shadow_CullEndThunk:\n"
    "    pushal\n"
    "    pushl %edi\n"
    "    call Shadow_AddCull\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl %edi, %ecx\n"      // overwritten original instructions: mov ecx, edi
    "    popl %edi\n"            //                                     pop edi
    "    jmp *g_shDoCulling\n"   //                                     jmp DoCulling
);
#endif


// Project a world point into the shadow map (for the diagnostics log).
static void ShProj(const float* m, float x, float y, float z, float* o)
{
    float v[4];
    for (int c = 0; c < 4; c++) v[c] = x * m[c] + y * m[4 + c] + z * m[8 + c] + m[12 + c];
    o[0] = v[0] / v[3]; o[1] = v[1] / v[3]; o[2] = v[2] / v[3]; o[3] = v[3];
}

static void Shadow_Diag(const char* why)
{
    BYTE* v1 = (BYTE*)0x00B4AF90u;
    BYTE* cam = *(BYTE**)(v1 + 0x40);
    const float* pv = *(const float**)v1;            // platinfo of view 1: +0 = view matrix
    const float* vp = (const float*)(0x00B47A90u + 16 * 0x1E0 + 0x140);
    float T[16]; const float h = 0.5f + 0.5f / (float)g_shSize;
    for (int r = 0; r < 4; r++) {
        T[r * 4 + 0] = vp[r * 4 + 0] * 0.5f + vp[r * 4 + 3] * h;
        T[r * 4 + 1] = vp[r * 4 + 1] * -0.5f + vp[r * 4 + 3] * h;
        T[r * 4 + 2] = vp[r * 4 + 2]; T[r * 4 + 3] = vp[r * 4 + 3];
    }
    if (!cam || !pv) return;
    float cx = *(float*)(cam + 0x40), cy = *(float*)(cam + 0x44), cz = *(float*)(cam + 0x48);
    float fx = pv[2], fy = pv[6], fz = pv[10];      // camera forward (view matrix column 3)
    Log("diag (%s): cam (%.1f %.1f %.1f) fwd (%.2f %.2f %.2f) detail %d hw %d fx %p debug %d\n",
        why, cx, cy, cz, fx, fy, fz, *(int*)SH_G_DETAIL, *(int*)SH_G_HWMODE, g_shFx, g_shDebug);
    Log("      handle: map %p T %p params %p filter %p\n", FxParam(g_shFx, "mwShadowMap"),
        FxParam(g_shFx, "mwShadowTransform"), FxParam(g_shFx, "mwShadowParams"), FxParam(g_shFx, "mwShadowFilter"));
    // matrices the engine uses to draw casters: eGetRenderTarget(16) (0xB47A90 + 16*0x1E0)
    // +0xC0 view, +0x100 proj, +0x140 viewproj, +0x190 viewproj alt. Compare with the platinfo of view 16.
    BYTE* P  = *(BYTE**)SH_VIEW_SHADOW1;
    BYTE* rv = (BYTE*)(0x00B47A90u + 16 * 0x1E0);
    Log("      platinfo16 %p, rv16 %p (rv+0xC0 = %p), rv16->platinfo %p\n", P, rv, rv + 0xC0, *(void**)(rv + 0x1D0));
    const int offs[] = { 0x60, 0xC0, 0x100, 0x140, 0x190 };
    for (int k = 0; k < 5; k++) {
        const float* m = (const float*)(rv + offs[k]);
        Log("      rv+%03X = [%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]\n", offs[k],
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    for (int k = 0; k < 3; k++) {
        const float* m = (const float*)(P + 0x40 * k);
        Log("      P+%03X  = [%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]\n", 0x40 * k,
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    if (g_carFx) {
        // the LAST paint material values set by the engine (usually the last car part drawn)
        static const char* mats[] = { "cvDiffuseMin", "cvDiffuseRange", "cvSpecularMin", "cvSpecularRange",
                                      "cvEnvmapMin", "cvEnvmapRange", "cvPowers", "cvClampAndScales", "cvFlakes", "cvVinylScales" };
        for (int k = 0; k < 10; k++) {
            float v[4] = {0};
            if (FxGetVector(g_carFx, mats[k], v))
                Log("      car %-16s = (%.3f %.3f %.3f %.3f)\n", mats[k], v[0], v[1], v[2], v[3]);
        }
    }
    static const float dist[] = { 0, 5, 15, 30, 50, 80 };
    for (int k = 0; k < 6; k++) {
        float o[4];
        ShProj(T, cx + fx * dist[k], cy + fy * dist[k], cz + fz * dist[k] - 1.5f, o);
        Log("      %2.0f m ahead, 1.5 m below the camera -> shadow (%.3f %.3f) z %.4f w %.2f\n", dist[k], o[0], o[1], o[2], o[3]);
    }
}

// 0x72CA4F: mov eax,[esi+0x40] ; fld [eax+0x40]  -> eax = fake camera (centre of the shadowed area)
#ifdef CMWV_MSASM
NAKED void Shadow_CamCave(void)
{
    __asm {
        lea eax, g_shFakeCam
        fld dword ptr [eax + 0x40]
        push 0x0072CA55
        ret
    }
}
#else
void Shadow_CamCave(void) ASMNAME("Shadow_CamCave");
__asm__(
    ".text\n"
    ".globl Shadow_CamCave\n"
    "Shadow_CamCave:\n"
    "    movl $g_shFakeCam, %eax\n"
    "    flds 0x40(%eax)\n"
    "    pushl $0x0072CA55\n"
    "    ret\n"
);
#endif

// Car caster pass in RenderShadowMap: Carbon draws every caster with reversed culling
// ([0xA6523C] = 1 -> back faces). That reduces acne for scenery, but for cars the stored depth
// becomes the car's underside (~0.2-0.3 m above the road) -> almost equal to the road, so car
// shadows break up. Cars are drawn with normal culling (mode 2 = faces towards the sun).
#ifdef CMWV_MSASM
NAKED void Shadow_CarPassBegin(void)        // replaces 0x72D6F7: mov eax,[0xA73008]
{
    __asm {
        mov dword ptr ds:[0x00A6523C], 2
        mov eax, dword ptr ds:[0x00A73008]
        push 0x0072D6FC
        ret
    }
}
NAKED void Shadow_CarPassEnd(void)          // replaces 0x72D861: mov dword [0xA73010],1
{
    __asm {
        mov dword ptr ds:[0x00A6523C], 1
        mov dword ptr ds:[0x00A73010], 1
        push 0x0072D86B
        ret
    }
}
#else
void Shadow_CarPassBegin(void) ASMNAME("Shadow_CarPassBegin");
void Shadow_CarPassEnd(void) ASMNAME("Shadow_CarPassEnd");
__asm__(
    ".text\n"
    ".globl Shadow_CarPassBegin\n"
    "Shadow_CarPassBegin:\n"                  // replaces 0x72D6F7: mov eax,[0xA73008]
    "    movl $2, 0x00A6523C\n"
    "    movl 0x00A73008, %eax\n"
    "    pushl $0x0072D6FC\n"
    "    ret\n"
    ".globl Shadow_CarPassEnd\n"
    "Shadow_CarPassEnd:\n"                    // replaces 0x72D861: mov dword [0xA73010],1
    "    movl $1, 0x00A6523C\n"
    "    movl $1, 0x00A73010\n"
    "    pushl $0x0072D86B\n"
    "    ret\n"
);
#endif

void __cdecl Shadow_DisplayFrame(void* view, void* cull)
{
    int did = 0;
    int diag = 0;
    Menu_Frame();   // installs the overlay hooks once the device exists
    if (g_debugMode) {   // debug hotkeys ([Debug] in the .ini); inactive when DebugMode=0
        if (KeyPressed(g_keyReload)) FE_Reload();
        if (KeyPressed(g_keyShadowView))   { g_shDebug = (g_shDebug + 1) % 4; Log("Debug: shadow debug view %d\n", g_shDebug); }
        if (KeyPressed(g_keyShadowToggle)) { g_shUser = !g_shUser;          Log("Debug: shadows %s\n", g_shUser ? "on" : "off"); }
        diag = KeyPressed(g_keyDiag);
        Lights_DebugUpdate();
    }
    TOD_Update();       // clock change -> fx values back before the FE module applies its own
    FE_Update();
    TOD_UpdatePost();   // in-game / FE time of day
    FE_EnvUpdate();
    if (g_shEnable && g_shOn && g_shUser && g_shFx && g_todShadowScale > 0.001f && *(int*)SH_G_DETAIL > 0 && *(int*)SH_G_HWMODE &&
        *(int*)SH_G_GAMEMODE == 6 && *(int*)SH_G_CULL_ON &&
        *(void**)SH_G_DEPTHTEX && *(void**)SH_G_DEPTHSURF && *(void**)SH_G_COLORSURF &&
        *(BYTE**)SH_VIEW_SHADOW1)
    {
        // sun direction: world.fx mwSunDirection (lighting and shadows must match)
        float* sun = NULL; float saved[3] = {0}, dir[4] = {0};
        BYTE* sunInfo = *(BYTE**)SH_G_SUNINFO;
        if (sunInfo && g_shFxWorld && FxGetVector(g_shFxWorld, "mwSunDirection", dir)) {
            float src[4] = {0};
            if (!(FxGetVector(g_shFxWorld, "mwSunSource", src) && src[0] > 0.5f)) {
                sun = (float*)(sunInfo + 0x160);
                memcpy(saved, sun, sizeof(saved));
                memcpy(sun, dir, sizeof(saved));
            }
        }
        // Shadowed area: sun camera FOV (at 2000 m) from Range, centre shifted ahead of the camera.
        // [0xA652A4] = FOV (degrees) used by sub_71EE50 for player 1's shadow view camera.
        *(float*)0x00A652A4u = (float)(2.0 * atan(g_shRange / 2000.0) * 57.29577951);
        {
            BYTE* cam = *(BYTE**)((BYTE*)view + 0x40);
            const float* pv = *(const float**)view;
            float fx = pv ? pv[2] : 0.0f, fy = pv ? pv[6] : 0.0f;
            float l = (float)sqrt(fx * fx + fy * fy);
            float k = (l > 1e-4f) ? g_shRange * g_shForward / l : 0.0f;
            g_shFakeCam[16] = *(float*)(cam + 0x40) + fx * k;
            g_shFakeCam[17] = *(float*)(cam + 0x44) + fy * k;
            g_shFakeCam[18] = *(float*)(cam + 0x48);
        }
        FxSetTexture(g_shFx, "mwShadowMap", NULL);   // do not sample the RT that is being written
        Shadow_SetParams(0);
        ((PFN_RenderShadow)SH_FN_RENDER_SHADOW)(view, cull, 0);
        if (sun) memcpy(sun, saved, sizeof(saved));

        // The engine draws casters with the render-view matrices of eGetRenderTarget(16) (0xB47A90 + 16*0x1E0):
        // +0x100 view, +0xC0 proj, +0x140 viewproj (lookAt camera from the sun direction, near 1650 /
        // far 2250, narrow FOV). The LiSPSM matrices RenderShadowMap writes to the platinfo of view 16
        // (leftover MW code) are NOT used by Carbon for drawing -> the shaders must not use them either.
        // mwShadowTransform = viewproj * texture scale (0.5, -0.5) + offset 0.5 + half a texel.
        const float* vp = (const float*)(0x00B47A90u + 16 * 0x1E0 + 0x140);
        float m[16];
        const float h = 0.5f + 0.5f / (float)g_shSize;
        for (int r = 0; r < 4; r++) {
            m[r * 4 + 0] = vp[r * 4 + 0] * 0.5f + vp[r * 4 + 3] * h;
            m[r * 4 + 1] = vp[r * 4 + 1] * -0.5f + vp[r * 4 + 3] * h;
            m[r * 4 + 2] = vp[r * 4 + 2];
            m[r * 4 + 3] = vp[r * 4 + 3];
        }
        FxSetMatrix(g_shFx, "mwShadowTransform", m);
        FxSetTexture(g_shFx, "mwShadowMap", *(void**)SH_G_DEPTHTEX);
        Shadow_SetParams(1);
        did = 1;
        if (diag || (g_debugMode && g_shFrames == 30)) Shadow_Diag(diag ? "hotkey" : "frame 30");
        if (g_shFrames < g_shLogFrames) {
            Log("shadow frame %d: mode %d, cull adds %d, sun (%.3f %.3f %.3f)\n"
                "  T = [%.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g | %.4g %.4g %.4g %.4g]\n",
                g_shFrames, *(int*)SH_G_HWMODE, g_shCullAdds, dir[0], dir[1], dir[2],
                m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
        }
        g_shFrames++;
    }
    if (!did && g_shReady) Shadow_SetParams(0);
    ((PFN_DisplayFrame)SH_FN_DISPLAY_FRAME)(view, cull);
    if (did) {
        // Release again: effects must not hold a D3DPOOL_DEFAULT RT reference outside the
        // main view render (device Reset / alt-tab). Passes before the next frame's
        // eDisplayFrame (envmap, reflections) run without shadows.
        FxSetTexture(g_shFx, "mwShadowMap", NULL);
        Shadow_SetParams(0);
    }
}

// Init: after the graphics option clamp, before the RTs are created (0x730BF1).
#define FOURCC_INTZ 0x5A544E49u
#define FOURCC_DF16 0x36314644u
#define SH_ADDR_DF16_IMM 0x007151C6u   // push 0x36314644 (DF16) in the shadow RT creator, mode 2

void __cdecl Shadow_InitClamp(void)
{
    ((PFN_Void)SH_FN_CLAMP)();
    if (!g_shEnable) return;
    int mode = *(int*)SH_G_HWMODE;
    void* d3d = *(void**)SH_G_D3D;
    typedef HRESULT (WINAPI *CheckFmt)(void*, UINT, DWORD, DWORD, DWORD, DWORD, DWORD);
    // same as Carbon 0x72C1CB: (adapter = [0xAB0AFC], HAL, X8R8G8B8, DEPTHSTENCIL, TEXTURE, fmt)
    #define SUPPORTS(fmt) (d3d && SUCCEEDED(((CheckFmt)(*(void***)d3d)[10])(d3d, *(DWORD*)SH_G_ADAPTERFMT, 1, 22, 2, 3, (fmt))))
    if (d3d) {
        // GetAdapterIdentifier (vtable 5): Driver[512], Description[512], DeviceName[32], ver, Vendor, Device...
        static BYTE id[1200];
        typedef HRESULT (WINAPI *GetId)(void*, UINT, DWORD, void*);
        if (SUCCEEDED(((GetId)(*(void***)d3d)[5])(d3d, *(DWORD*)SH_G_ADAPTERFMT, 0, id)))
            Log("GPU: %s (vendor 0x%04lX), Carbon shadow mode %d, D24S8 %d INTZ %d DF16 %d\n",
                (char*)(id + 512), (unsigned long)*(DWORD*)(id + 512 + 512 + 32 + 8), mode,
                SUPPORTS(75) ? 1 : 0, SUPPORTS(FOURCC_INTZ) ? 1 : 0, SUPPORTS(FOURCC_DF16) ? 1 : 0);
    }
    if (g_shFormat == 1 && SUPPORTS(75)) mode = 1;
    else if (g_shFormat == 2 && (SUPPORTS(FOURCC_INTZ) || SUPPORTS(FOURCC_DF16))) {
        // depth texture sampled as a value (manual compare in the shader)
        if (SUPPORTS(FOURCC_INTZ)) {
            DWORD old; BYTE* imm = (BYTE*)SH_ADDR_DF16_IMM;
            if (imm[-1] == 0x68 && *(DWORD*)imm == FOURCC_DF16) {
                VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old);
                *(DWORD*)imm = FOURCC_INTZ;
                VirtualProtect(imm, 4, old, &old);
                Log("shadow: format INTZ\n");
            }
        }
        mode = 2;
    }
    else if (mode == 0 && g_shForceHW && SUPPORTS(75)) mode = 1;   // GPUs Carbon does not recognise
    *(int*)SH_G_HWMODE = mode;
    if (mode == 0) {
        Log("shadow: GPU does not support depth textures -> shadows OFF\n");
        g_shEnable = 0;
        return;
    }
    if (*(int*)SH_G_DETAIL_MAX < g_shDetail) *(int*)SH_G_DETAIL_MAX = g_shDetail;
    *(int*)SH_G_DETAIL = g_shDetail;
    g_shReady = 1;
    Log("shadow: hw mode %d, g_ShadowDetail = %d\n", mode, g_shDetail);
}

void Shadow_Install(void)
{
    const char* ini = g_iniPath;
    g_shOn        = GetPrivateProfileIntA("Shadows", "Enable", 1, ini) != 0;
    g_shEnable    = 1;   // the module (hooks) is always installed; Enable only switches the shadows (runtime)
    FE_Init(ini);
    TOD_Init();
    Menu_Init();
    if (g_debugMode) {
        g_keyReload       = Cfg_Key("Debug", "ReloadKey", "F3");
        g_keyShadowView   = Cfg_Key("Debug", "ShadowDebugViewKey", "F9");
        g_keyShadowToggle = Cfg_Key("Debug", "ShadowToggleKey", "F10");
        g_keyDiag         = Cfg_Key("Debug", "DiagnosticsKey", "F6");
        Log("debug hotkeys (VK): reload 0x%02X, shadow view 0x%02X, shadow toggle 0x%02X, diagnostics 0x%02X\n",
            g_keyReload, g_keyShadowView, g_keyShadowToggle, g_keyDiag);
    }
    g_shDetail    = GetPrivateProfileIntA("Shadows", "Detail", 2, ini);
    g_shForceHW   = GetPrivateProfileIntA("Shadows", "ForceHW", 1, ini);
    g_shLogFrames = g_debugMode ? GetPrivateProfileIntA("Debug", "LogFrames", 3, ini) : 0;
    g_shFormat    = GetPrivateProfileIntA("Shadows", "Format", 0, ini);
    g_shCasters   = GetPrivateProfileIntA("Shadows", "Casters", 0, ini);
    g_shSize      = GetPrivateProfileIntA("Shadows", "Size", 2048, ini);
    if (g_shSize != 1024 && g_shSize != 2048 && g_shSize != 4096) g_shSize = 2048;
    g_shSizeIni = g_shSize;
    char buf[128];
    GetPrivateProfileStringA("Shadows", "Strength", "0.85", buf, sizeof(buf), ini); g_shStrength = (float)atof(buf);
    GetPrivateProfileStringA("Shadows", "Range", "90", buf, sizeof(buf), ini);     g_shRange = (float)atof(buf);
    GetPrivateProfileStringA("Shadows", "Darkness", "0.4", buf, sizeof(buf), ini); g_shDarkness = (float)atof(buf);
    GetPrivateProfileStringA("Shadows", "Softness", "1.0", buf, sizeof(buf), ini); g_shSoftness = (float)atof(buf);
    if (g_shDarkness < 0) g_shDarkness = 0;
    if (g_shDarkness > 1) g_shDarkness = 1;
    if (g_shSoftness < 0.25f) g_shSoftness = 0.25f;
    if (g_shSoftness > 4.0f) g_shSoftness = 4.0f;
    GetPrivateProfileStringA("Shadows", "Forward", "0.4", buf, sizeof(buf), ini);  g_shForward = (float)atof(buf);
    if (g_shStrength < 0) g_shStrength = 0;
    if (g_shStrength > 1) g_shStrength = 1;
    if (g_shRange < 20) g_shRange = 20;
    if (g_shRange > 400) g_shRange = 400;
    GetPrivateProfileStringA("Sun", "Direction", "", buf, sizeof(buf), ini);
    if (sscanf(buf, "%f , %f , %f", &g_shSun[0], &g_shSun[1], &g_shSun[2]) == 3) {
        float l = (float)sqrt(g_shSun[0] * g_shSun[0] + g_shSun[1] * g_shSun[1] + g_shSun[2] * g_shSun[2]);
        if (l > 1e-4f) { g_shSun[0] /= l; g_shSun[1] /= l; g_shSun[2] /= l; g_shSunSet = 1; }
    }
    if (g_shDetail < 1) g_shDetail = 1;
    if (g_shDetail > 2) g_shDetail = 2;
    Log("shadow: Enable=%d (runtime switch) Detail=%d ForceHW=%d Format=%d Size=%d Strength=%.2f Darkness=%.2f Softness=%.2f Range=%.0f Forward=%.2f Sun=%s\n",
        g_shOn, g_shDetail, g_shForceHW, g_shFormat, g_shSize, g_shStrength, g_shDarkness, g_shSoftness, g_shRange, g_shForward,
        g_shSunSet ? buf : "(from fx)");

    static const BYTE cullEnd[8] = { 0x8B, 0xCF, 0x5F, 0xE9, 0xB9, 0x46, 0x09, 0x00 };
    BYTE* ce = (BYTE*)SH_ADDR_CULL_END;
    if (memcmp(ce, cullEnd, 8) != 0) { Log("shadow: ERROR culling end bytes do not match, cancelled\n"); g_shEnable = 0; return; }
    if (!PatchCall(SH_ADDR_CALL_CLAMP, SH_FN_CLAMP, (void*)Shadow_InitClamp) ||
        !PatchCall(SH_ADDR_CALL_DISPLAY, SH_FN_DISPLAY_FRAME, (void*)Shadow_DisplayFrame)) { g_shEnable = 0; return; }
    // Scenery caster filter in RenderShadowMap: flag 0x2000 (detail 1) / 0x200 (detail 2) makes
    // StuffScenery take only instances marked "cast shadow" (data bits 0x100000/0x200000/
    // 0x40000000). Carbon's track data does not seem to have those marks -> empty shadow map.
    // Casters=0: flag 0 = every scenery instance that passes the shadow view culling casts.
    if (g_shCasters == 0) {
        BYTE* p1 = (BYTE*)0x0072D6A1u; BYTE* p2 = (BYTE*)0x0072D6A8u;
        if (p1[0] == 0x68 && *(DWORD*)(p1 + 1) == 0x2000 && p2[0] == 0x68 && *(DWORD*)(p2 + 1) == 0x200) {
            DWORD o2;
            VirtualProtect(p1, 12, PAGE_EXECUTE_READWRITE, &o2);
            *(DWORD*)(p1 + 1) = 0; *(DWORD*)(p2 + 1) = 0;
            VirtualProtect(p1, 12, o2, &o2);
            Log("shadow: casters = all scenery (Casters=0)\n");
        } else Log("shadow: WARNING caster flag bytes do not match, not patched\n");
    }
    // Shadow map size: immediate 0x400 in the RT creator (0x715150), RTinfo (0x71AFC3), scale (0x715090)
    if (g_shSize != 1024) {
        static const DWORD imm[] = { 0x715167, 0x71516C, 0x715199, 0x71519E, 0x7151CF, 0x7151D4, 0x71AFC4, 0x715091 };
        int ok = 1;
        for (int k = 0; k < 8; k++) if (*(DWORD*)imm[k] != 0x400) ok = 0;
        if (ok) {
            for (int k = 0; k < 8; k++) {
                DWORD o3; VirtualProtect((void*)imm[k], 4, PAGE_EXECUTE_READWRITE, &o3);
                *(DWORD*)imm[k] = (DWORD)g_shSize;
                VirtualProtect((void*)imm[k], 4, o3, &o3);
            }
            Log("shadow: map size %d\n", g_shSize);
        } else { Log("shadow: WARNING size immediates do not match, staying at 1024\n"); g_shSize = 1024; g_shSizeIni = 1024; }
    }
    {
        static const BYTE camOrig[6] = { 0x8B, 0x46, 0x40, 0xD9, 0x40, 0x40 };
        BYTE* cc = (BYTE*)0x0072CA4Fu;
        if (memcmp(cc, camOrig, 6) == 0) {
            DWORD o4; VirtualProtect(cc, 6, PAGE_EXECUTE_READWRITE, &o4);
            cc[0] = 0xE9; *(LONG*)(cc + 1) = (LONG)((DWORD)Shadow_CamCave - (0x0072CA4Fu + 5)); cc[5] = 0x90;
            VirtualProtect(cc, 6, o4, &o4);
            FlushInstructionCache(GetCurrentProcess(), cc, 6);
        } else Log("shadow: WARNING shadow camera bytes do not match, area not shifted\n");
    }
    if (GetPrivateProfileIntA("Shadows", "CarFrontFaces", 1, ini)) {
        static const BYTE b1[5] = { 0xA1, 0x08, 0x30, 0xA7, 0x00 };
        static const BYTE b2[10] = { 0xC7, 0x05, 0x10, 0x30, 0xA7, 0x00, 0x01, 0x00, 0x00, 0x00 };
        BYTE* p1 = (BYTE*)0x0072D6F7u; BYTE* p2 = (BYTE*)0x0072D861u;
        if (!memcmp(p1, b1, 5) && !memcmp(p2, b2, 10)) {
            DWORD o5;
            VirtualProtect(p1, 5, PAGE_EXECUTE_READWRITE, &o5);
            p1[0] = 0xE9; *(LONG*)(p1 + 1) = (LONG)((DWORD)Shadow_CarPassBegin - (0x0072D6F7u + 5));
            VirtualProtect(p1, 5, o5, &o5);
            VirtualProtect(p2, 10, PAGE_EXECUTE_READWRITE, &o5);
            p2[0] = 0xE9; *(LONG*)(p2 + 1) = (LONG)((DWORD)Shadow_CarPassEnd - (0x0072D861u + 5));
            memset(p2 + 5, 0x90, 5);
            VirtualProtect(p2, 10, o5, &o5);
            FlushInstructionCache(GetCurrentProcess(), (void*)0x0072D000u, 0x1000);
            Log("shadow: cars drawn into the shadow map with normal culling (CarFrontFaces=1)\n");
        } else Log("shadow: WARNING car pass bytes do not match, CarFrontFaces skipped\n");
    }
    DWORD old;
    VirtualProtect(ce, 8, PAGE_EXECUTE_READWRITE, &old);
    ce[0] = 0xE9;
    *(LONG*)(ce + 1) = (LONG)((DWORD)Shadow_CullEndThunk - (SH_ADDR_CULL_END + 5));
    ce[5] = ce[6] = ce[7] = 0x90;
    VirtualProtect(ce, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), ce, 8);
    Log("shadow: hooks installed (cull 0x%08X, display 0x%08X, init 0x%08X)\n",
        SH_ADDR_CULL_END, SH_ADDR_CALL_DISPLAY, SH_ADDR_CALL_CLAMP);
}


// --- menu (Global tab) -------------------------------------------------------------------------
// Enable switches at runtime. The map size is patched into Carbon's RT creation at startup, so a
// new Size is saved to the .ini and used from the next game start.
void Shadow_GetSettings(int* on, int* size, int* sizeActive, int* available)
{
    if (on) *on = g_shOn;
    if (size) *size = g_shSizeIni;
    if (sizeActive) *sizeActive = g_shSize;
    if (available) *available = g_shEnable && g_shReady;
}

void Shadow_SetSettings(int on, int size)
{
    on = on != 0;
    if (size != 1024 && size != 2048 && size != 4096) size = g_shSizeIni;
    if (on != g_shOn || size != g_shSizeIni) Log("shadow: Enable=%d Size=%d (menu)\n", on, size);
    g_shOn = on; g_shSizeIni = size;
}

void Shadow_SaveIni(void)
{
    char b[16];
    WritePrivateProfileStringA("Shadows", "Enable", g_shOn ? "1" : "0", g_iniPath);
    snprintf(b, sizeof(b), "%d", g_shSizeIni);
    WritePrivateProfileStringA("Shadows", "Size", b, g_iniPath);
}

void Shadow_ReloadIni(void)
{
    int size = GetPrivateProfileIntA("Shadows", "Size", 2048, g_iniPath);
    if (size != 1024 && size != 2048 && size != 4096) size = 2048;
    Shadow_SetSettings(GetPrivateProfileIntA("Shadows", "Enable", 1, g_iniPath), size);
}
