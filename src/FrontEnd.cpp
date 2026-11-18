// FrontEnd.cpp -- separate settings for the menus/garage (FE) + FE reflection cubemap from NFS Most Wanted
#include "pch.h"
#include "Common.h"

// --- [FrontEnd] / [FrontEndFx]: separate settings for the menus/garage (GameFlowState [0xA99BBC] == 3) ---
// Registered port effects: those with mwDayBlend or mwHeadlightScale. While in the FE:
//  - mwDayBlend per effect kind = [FrontEnd] World/Car/VTDayBlend
//  - parameters in [FrontEndFx] (name=value, or file.name=value for a single file, e.g. car.mwShadowBias)
// Outside the FE everything is restored to the values read from the fx files when they were loaded.
enum { DB_WORLD = 0, DB_CAR = 1, DB_VT = 2 };
#define FE_MAXFX 48
#define FE_MAXOV 128
static void* g_dbFx[FE_MAXFX]; static void* g_dbH[FE_MAXFX]; static float g_dbOrig[FE_MAXFX]; static int g_dbKind[FE_MAXFX];
static char  g_dbName[FE_MAXFX][40]; static int g_dbNum = 0;
static int   g_feEnable = 1, g_feState = -1, g_feDirty = 1;
static float g_feBlend[3] = { 1.0f, 1.0f, 1.0f };   // world / car / visualtreatment while in the FE
static char  g_feIni[MAX_PATH];
static int   g_feEnvOn = 1, g_feEnvDone = 0;   // FE reflection cubemap from MW (done = installed now)
static int   g_feGen = 0;                       // bumped when effects or FE settings change
static char  g_feEnvSet[32] = "";
void FE_OnEffect(const char* res, void* fx)
{
    void* h = FxParam(fx, "mwDayBlend");
    if ((!h && !FxParam(fx, "mwHeadlightScale")) || g_dbNum >= FE_MAXFX) return;
    typedef ULONG (WINAPI *AddRefFn)(void*);
    ((AddRefFn)(*(void***)fx)[1])(fx);
    float v = 1.0f;
    if (h) ((FX_GetFloat)(*(void***)fx)[31])(fx, h, &v);
    int k = DB_WORLD;
    if (res && (!strcmp(res, "IDI_CAR_FX") || !strcmp(res, "IDI_CARNORMALMAP_FX"))) k = DB_CAR;
    if (res && !strcmp(res, "IDI_VISUALTREATMENT_FX")) k = DB_VT;
    // short name = fx file name: IDI_CARNORMALMAP_FX -> carnormalmap
    char* n = g_dbName[g_dbNum]; n[0] = 0;
    if (res && !strncmp(res, "IDI_", 4)) {
        int j = 0;
        for (const char* c = res + 4; *c && j < 39; c++) n[j++] = (char)tolower((unsigned char)*c);
        n[j] = 0;
        int L = (int)strlen(n); if (L > 3 && !strcmp(n + L - 3, "_fx")) n[L - 3] = 0;
    }
    g_dbFx[g_dbNum] = fx; g_dbH[g_dbNum] = h; g_dbOrig[g_dbNum] = v; g_dbKind[g_dbNum] = k; g_dbNum++;
    g_feDirty = 1; g_feGen++;
}
typedef struct { char scope[40]; char name[64]; float v[4]; int n; } FEOverride;
static FEOverride g_feOv[FE_MAXOV]; static int g_feNumOv = 0;
static float g_feOvOrig[FE_MAXFX][FE_MAXOV][4]; static unsigned char g_feOvHas[FE_MAXFX][FE_MAXOV];
static void FE_ReadIni(void)
{
    const char* ini = g_feIni;
    char b[64];
    g_feEnable = GetPrivateProfileIntA("FrontEnd", "Enable", 1, ini);
    GetPrivateProfileStringA("FrontEnd", "WorldDayBlend", "1.0", b, sizeof(b), ini); g_feBlend[DB_WORLD] = (float)atof(b);
    GetPrivateProfileStringA("FrontEnd", "CarDayBlend", "1.0", b, sizeof(b), ini);   g_feBlend[DB_CAR] = (float)atof(b);
    GetPrivateProfileStringA("FrontEnd", "VTDayBlend", "1.0", b, sizeof(b), ini);    g_feBlend[DB_VT] = (float)atof(b);
    g_feEnvOn = GetPrivateProfileIntA("FrontEnd", "MWEnvMap", 1, ini);
    GetPrivateProfileStringA("FrontEnd", "MWEnvMapSet", "", g_feEnvSet, sizeof(g_feEnvSet), ini);
    g_feEnvDone = 0;
    Log("FE: MWEnvMap=%d MWEnvMapSet='%s'\n", g_feEnvOn, g_feEnvSet);
    Log("FE: Enable=%d WorldDayBlend=%.2f CarDayBlend=%.2f VTDayBlend=%.2f\n", g_feEnable,
        g_feBlend[DB_WORLD], g_feBlend[DB_CAR], g_feBlend[DB_VT]);
    static char sec[32768];
    g_feNumOv = 0;
    DWORD len = GetPrivateProfileSectionA("FrontEndFx", sec, sizeof(sec), ini);
    for (char* l = sec; len && *l && g_feNumOv < FE_MAXOV; l += strlen(l) + 1) {
        while (*l == ' ' || *l == '\t') l++;
        if (*l == ';' || *l == '#' || !*l) continue;
        char* eq = strchr(l, '='); if (!eq) continue;
        FEOverride* o = &g_feOv[g_feNumOv];
        int nl = (int)(eq - l); while (nl > 0 && (l[nl - 1] == ' ' || l[nl - 1] == '\t')) nl--;
        if (nl <= 0 || nl >= 100) continue;
        char key[100]; memcpy(key, l, nl); key[nl] = 0;
        char* dot = strchr(key, '.');
        o->scope[0] = 0;
        if (dot) { *dot = 0; snprintf(o->scope, sizeof(o->scope), "%s", key); for (char* c = o->scope; *c; c++) *c = (char)tolower((unsigned char)*c); snprintf(o->name, sizeof(o->name), "%s", dot + 1); }
        else snprintf(o->name, sizeof(o->name), "%s", key);
        o->v[0] = o->v[1] = o->v[2] = o->v[3] = 0.0f;
        o->n = sscanf(eq + 1, " %f , %f , %f , %f", &o->v[0], &o->v[1], &o->v[2], &o->v[3]);
        if (o->n < 1) continue;
        g_feNumOv++;
    }
    Log("FE: %d parameters in [FrontEndFx]\n", g_feNumOv);
    memset(g_feOvHas, 0, sizeof(g_feOvHas));
    g_feDirty = 1; g_feGen++;
}

int FE_Generation(void) { return g_feGen; }

// The value the FE gives a parameter of an effect ([FrontEnd] *DayBlend, [FrontEndFx]); 0 = not set by the FE.
int FE_ValueFor(void* fx, const char* param, float out[4])
{
    if (!g_feEnable) return 0;
    int e = -1;
    for (int i = 0; i < g_dbNum; i++) if (g_dbFx[i] == fx) e = i;
    if (e < 0) return 0;
    int found = 0;
    if (g_dbH[e] && !strcmp(param, "mwDayBlend")) { out[0] = g_feBlend[g_dbKind[e]]; found = 1; }
    for (int i = 0; i < g_feNumOv; i++) {
        if (g_feOv[i].scope[0] && strcmp(g_feOv[i].scope, g_dbName[e])) continue;
        if (strcmp(g_feOv[i].name, param)) continue;
        if (g_feOv[i].n == 1) out[0] = g_feOv[i].v[0];
        else memcpy(out, g_feOv[i].v, 4 * sizeof(float));
        found = 1;
    }
    return found;
}
static void FE_ApplyOverrides(int fe)
{
    for (int e = 0; e < g_dbNum; e++) {
        void* fx = g_dbFx[e];
        for (int i = 0; i < g_feNumOv; i++) {
            if (g_feOv[i].scope[0] && strcmp(g_feOv[i].scope, g_dbName[e])) continue;
            void* h = FxParam(fx, g_feOv[i].name);
            if (!h) continue;
            float* orig = g_feOvOrig[e][i];
            if (!g_feOvHas[e][i]) {
                orig[0] = orig[1] = orig[2] = orig[3] = 0.0f;
                if (g_feOv[i].n == 1) ((FX_GetFloat)(*(void***)fx)[31])(fx, h, orig);
                else ((FX_GetVector)(*(void***)fx)[35])(fx, h, orig);
                g_feOvHas[e][i] = 1;
            }
            const float* val = fe ? g_feOv[i].v : orig;
            if (g_feOv[i].n == 1) ((FX_SetFloat)(*(void***)fx)[30])(fx, h, val[0]);
            else ((FX_SetVector)(*(void***)fx)[34])(fx, h, val);
        }
    }
}
// --- FE reflection cubemap from MW ------------------------------------------------------------
// Carbon (like MW) fills the car reflection cubemap in the FE from 6 static textures
// "<PREFIX>_XPOS.._ZNEG" (PREFIX = QRACE/CAREER/CSHOP/CARLOT depending on the garage) through
// EnvMapTextureLoadedCallback (0x70FC10): once all 6 are loaded ([0xAB0734] == 6) each face is
// copied into the cube [0xAB0954] level 0. The ASI then overwrites the cube faces with DDS files
// extracted from MW's GLOBAL\DYNTEX.BIN: scripts\CarbonMWVisuals_FEEnv\<PREFIX>_<FACE>.dds.
typedef HRESULT (WINAPI *PFN_LoadSurfFile)(void*, const void*, const RECT*, const char*, const RECT*, DWORD, DWORD, void*);
typedef int (THISCALL *PFN_GetGarageType)(void*);
typedef const char* (THISCALL *PFN_GetGaragePrefix)(void*, int);
// MWEnvMap switched off while the MW faces are in the cube: run Carbon's own callback again
// (counter back to 5, 0x70FC10 counts to 6 and copies the garage's <PREFIX>_<FACE> textures).
static void FE_EnvRestore(void)
{
    unsigned char cnt = *(volatile unsigned char*)0x00AB0734u;
    if (g_feEnvDone && cnt >= 6 && *(void**)0x00AB0954u && *(int*)0x00A99BBCu == 3) {
        *(volatile unsigned char*)0x00AB0734u = 5;
        ((void (__cdecl*)(void))0x0070FC10u)();
        Log("FE env: Carbon's own reflection cubemap restored\n");
    }
    g_feEnvDone = 0;
}

void FE_GetEnv(int* on, char* set, int setSize)
{
    *on = g_feEnvOn;
    snprintf(set, setSize, "%s", g_feEnvSet);
}

void FE_SetEnv(int on, const char* set)
{
    on = on ? 1 : 0;
    if (strcmp(set, g_feEnvSet)) { snprintf(g_feEnvSet, sizeof(g_feEnvSet), "%s", set); g_feEnvDone = 0; }
    g_feEnvOn = on;   // FE_EnvUpdate (per frame) installs or restores
}

void FE_SaveEnv(void)
{
    WritePrivateProfileStringA("FrontEnd", "MWEnvMap", g_feEnvOn ? "1" : "0", g_iniPath);
    WritePrivateProfileStringA("FrontEnd", "MWEnvMapSet", g_feEnvSet, g_iniPath);
}

void FE_EnvUpdate(void)
{
    if (!g_feEnvOn) { if (g_feEnvDone) FE_EnvRestore(); return; }
    unsigned char cnt = *(volatile unsigned char*)0x00AB0734u;
    void* cube = *(void**)0x00AB0954u;
    if (cnt < 6 || !cube) { g_feEnvDone = 0; return; }
    if (g_feEnvDone) return;
    g_feEnvDone = 1;
    static PFN_LoadSurfFile load = NULL;
    if (!load) {
        HMODULE d3dx = GetModuleHandleA("d3dx9_30.dll");
        if (d3dx) load = (PFN_LoadSurfFile)GetProcAddress(d3dx, "D3DXLoadSurfaceFromFileA");
        if (!load) { Log("FE env: D3DXLoadSurfaceFromFileA not found\n"); g_feEnvOn = 0; return; }
    }
    char pre[32] = "";
    if (g_feEnvSet[0]) snprintf(pre, sizeof(pre), "%s", g_feEnvSet);
    else {
        void* fem = *(void**)0x00A97A7Cu;
        if (fem) {
            int t = ((PFN_GetGarageType)0x00572BA0u)(fem);
            const char* p = ((PFN_GetGaragePrefix)0x00572C20u)(fem, t);
            if (p) snprintf(pre, sizeof(pre), "%s", p);
        }
    }
    if (!pre[0]) snprintf(pre, sizeof(pre), "QRACE");
    static const char* const faces[6] = { "XPOS", "XNEG", "YPOS", "YNEG", "ZPOS", "ZNEG" };
    typedef DWORD (WINAPI *GetLevelCountFn)(void*);
    typedef HRESULT (WINAPI *GetCubeSurfFn)(void*, int, UINT, void**);
    typedef ULONG (WINAPI *ReleaseFn)(void*);
    DWORD levels = ((GetLevelCountFn)(*(void***)cube)[13])(cube);
    int ok = 0;
    for (int f = 0; f < 6; f++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\scripts\\CarbonMWVisuals_FEEnv\\%s_%s.dds", g_gameDir, pre, faces[f]);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { Log("FE env: missing %s\n", path); continue; }
        int fok = 1;
        for (DWORD l = 0; l < levels; l++) {
            void* surf = NULL;
            if (FAILED(((GetCubeSurfFn)(*(void***)cube)[18])(cube, f, l, &surf)) || !surf) { fok = 0; continue; }
            if (FAILED(load(surf, NULL, NULL, path, NULL, 0x00000005 /*D3DX_FILTER_TRIANGLE*/, 0, NULL))) fok = 0;
            ((ReleaseFn)(*(void***)surf)[2])(surf);
        }
        ok += fok;
    }
    Log("FE env: MW reflection cubemap '%s' installed (%d/6 faces, %lu levels)\n", pre, ok, (unsigned long)levels);
}

// Debug reload key: re-read [FrontEnd] + [FrontEndFx] without restarting (old values restored first)
void FE_Reload(void)
{
    FE_ApplyOverrides(0);
    for (int i = 0; i < g_dbNum; i++) if (g_dbH[i]) ((FX_SetFloat)(*(void***)g_dbFx[i])[30])(g_dbFx[i], g_dbH[i], g_dbOrig[i]);
    FE_ReadIni();
    Lights_ReadWorldFlares(g_feIni);
    TOD_ReadIni(0);
    g_feState = -1;
    Log("Reload: FE settings re-read from the .ini\n");
}

void FE_Update(void)
{
    int fe = g_feEnable && *(int*)0x00A99BBCu == 3;
    if (fe == g_feState && !g_feDirty) return;
    for (int i = 0; i < g_dbNum; i++) if (g_dbH[i])
        ((FX_SetFloat)(*(void***)g_dbFx[i])[30])(g_dbFx[i], g_dbH[i],
            fe ? g_feBlend[g_dbKind[i]] : g_dbOrig[i]);
    FE_ApplyOverrides(fe);
    if (fe != g_feState)
        Log("FE: %s (mwDayBlend world %.2f, car %.2f, visualtreatment %.2f; %d effects)\n",
            fe ? "entered menus/garage" : "left the FE -> fx values", fe ? g_feBlend[0] : -1.0f,
            fe ? g_feBlend[1] : -1.0f, fe ? g_feBlend[2] : -1.0f, g_dbNum);
    g_feState = fe; g_feDirty = 0;
}

void FE_Init(const char* ini)
{
    snprintf(g_feIni, sizeof(g_feIni), "%s", ini);
    FE_ReadIni();
}

