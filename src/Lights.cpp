// Lights.cpp -- light patches: world light glows, AI headlight beams, headlight glow in reflections
#include "pch.h"
#include "Common.h"

// --- [Lights] -------------------------------------------------------------------------------
// eRenderLightFlare (0x74D330) draws every light flare/glow sprite. Callers:
//  - RenderFlaresOnCar (0x7CC641): car lights (untouched).
//  - eRenderLightFlarePool (0x750AAA) and the scenery flare list sub_74E150 (0x74E230): world
//    lights (street lamps, buildings, ...). WorldFlares=0 -> those two calls are skipped, except
//    for the flare types listed in WorldFlaresKeep.
// Arg 2 is the eLightFlare (0x30 bytes): +0x0C colour tint, +0x10 position, +0x2C type (index
// into the 32 "lightflare" VLT entries), +0x2D flags. In eRenderLightFlarePool, 17/29 blink with a
// random phase per position (4 timers at 0xB42F08), 18/19 blink on timer 0/2, 14/15/16 are skipped
// in reflection views, 21 = sun flare. Road hazard/warning lights (checked in game with
// FlareSoloKey): 16, 18, 29 -> default WorldFlaresKeep.
// flareType 2 = "HEADLIGHTGLOW": a fixed-shape glow sprite placed on the road in front of nearby
// AI cars (reflection views). HeadlightGlow=0: the glow texture reads as NULL
// (mov eax,[0xB4CFC4] -> xor eax,eax), so type-2 flares take the original skip path (je 0x74E10F).
typedef int (__cdecl *PFN_RenderLightFlare)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static int   g_worldFlares   = 1;
static DWORD g_flareKeepMask = 0;      // bit n = keep flare type n when WorldFlares=0
static int   g_flareSolo     = -1;     // debug: draw only this type (-1 = off)
static DWORD g_flareSeenMask = 0;      // debug: types drawn since the last solo key press
static int   g_keyFlareSolo  = 0;

static int FlareType(DWORD flare)
{
    return flare ? (int)*(const BYTE*)(flare + 0x2C) : -1;
}

static float g_flareNight = 0.0f;      // 0 = day, 1 = night ([TimeOfDay]): world lights fade in
static int   g_nightLights = 1;        // [Lights] NightLights

static int __cdecl Flare_World(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g, DWORD h, DWORD i, DWORD j)
{
    int t = FlareType(b);
    if (t >= 0 && t < 32) g_flareSeenMask |= 1u << t;
    float k;
    if (g_flareSolo >= 0) k = (t == g_flareSolo) ? 1.0f : 0.0f;
    else if (g_worldFlares || (t >= 0 && t < 32 && ((g_flareKeepMask >> t) & 1))) k = 1.0f;
    else k = g_nightLights ? g_flareNight : 0.0f;
    if (k <= 0.001f) return 0;
    if (k < 0.999f) { float v; memcpy(&v, &d, 4); v *= k; memcpy(&d, &v, 4); }   // arg 4 = intensity
    return ((PFN_RenderLightFlare)0x0074D330u)(a, b, c, d, e, f, g, h, i, j);
}

static void MaskToText(DWORD m, char* out, size_t n)
{
    size_t len = 0; out[0] = 0;
    for (int t = 0; t < 32 && len + 4 < n; t++)
        if ((m >> t) & 1) len += (size_t)snprintf(out + len, n - len, len ? ",%d" : "%d", t);
    if (!len) snprintf(out, n, "none");
}

void Lights_ReadWorldFlares(const char* ini)
{
    g_worldFlares = GetPrivateProfileIntA("Lights", "WorldFlares", 0, ini);
    char buf[256];
    GetPrivateProfileStringA("Lights", "WorldFlaresKeep", "16,18,29", buf, sizeof(buf), ini);
    g_flareKeepMask = 0;
    for (char* p = buf; *p; ) {
        if (*p == ';') break;
        if (*p >= '0' && *p <= '9') {
            long v = strtol(p, &p, 10);
            if (v >= 0 && v < 32) g_flareKeepMask |= 1u << v;
        } else p++;
    }
    char keep[128]; MaskToText(g_flareKeepMask, keep, sizeof(keep));
    Log("lights: world light glows %s (WorldFlares=%d), types kept when off: %s\n",
        g_worldFlares ? "on" : "OFF", g_worldFlares, keep);
}

// Debug (DebugMode=1): FlareSoloKey cycles through the world flare types seen since the last press,
// drawing only that type, so a light can be identified and added to WorldFlaresKeep.
void Lights_DebugUpdate(void)
{
    if (!g_debugMode || !KeyPressed(g_keyFlareSolo)) return;
    DWORD seen = g_flareSeenMask;
    if (g_flareSolo >= 0) seen |= 1u << g_flareSolo;
    int next = -1;
    for (int t = g_flareSolo + 1; t < 32; t++) if ((seen >> t) & 1) { next = t; break; }
    g_flareSolo = next;
    char s[128]; MaskToText(seen, s, sizeof(s));
    if (next >= 0) Log("Debug: world flares SOLO type %d (types seen: %s)\n", next, s);
    else           Log("Debug: world flares solo off (types seen: %s)\n", s);
    g_flareSeenMask = 0;
}

// Switchable code patch: applied in daytime, original bytes back at night (NightLights=1).
struct BytePatch { BYTE* at; BYTE orig[8]; BYTE patch[8]; int len; int ok; int on; const char* name; };
static BytePatch g_beamPatch = { (BYTE*)0x007BE170u, { 0x55, 0x8B, 0xEC }, { 0xC2, 0x0C, 0x00 }, 3, 0, 0,
                                 "AI textured headlight beams" };   // ret 0xC
static BytePatch g_glowPatch = { (BYTE*)0x0074DB30u, { 0xA1, 0xC4, 0xCF, 0xB4, 0x00 }, { 0x31, 0xC0, 0x90, 0x90, 0x90 }, 5, 0, 0,
                                 "AI headlight glow on the road" };  // mov eax,[0xB4CFC4] -> xor eax,eax

static void Patch_Set(BytePatch* bp, int on)
{
    if (!bp->ok || bp->on == on) return;
    DWORD o;
    VirtualProtect(bp->at, bp->len, PAGE_EXECUTE_READWRITE, &o);
    memcpy(bp->at, on ? bp->patch : bp->orig, bp->len);
    VirtualProtect(bp->at, bp->len, o, &o);
    FlushInstructionCache(GetCurrentProcess(), bp->at, bp->len);
    bp->on = on;
    if (g_debugMode) Log("lights: %s %s\n", bp->name, on ? "disabled" : "original");
}

// Called every frame by the time of day: 0 = day, 1 = night. At night the world lights fade in.
void Lights_SetNight(float night)
{
    g_flareNight = night;
}

// Called every frame by the time of day: car headlights on (from HeadlightsOn to HeadlightsOff)
// -> the AI headlight beams/glow come back (Carbon original); otherwise the [Lights] settings apply.
void Lights_SetHeadlights(int on)
{
    int day = !g_nightLights || !on;
    Patch_Set(&g_beamPatch, day);
    Patch_Set(&g_glowPatch, day);
}

void Lights_Install(void)
{
    const char* ini = g_iniPath;
    if (g_debugMode) {
        g_keyFlareSolo = Cfg_Key("Debug", "FlareSoloKey", "F8");
        Log("Debug: flare solo key vk=0x%02X\n", g_keyFlareSolo);
    }
    g_nightLights = GetPrivateProfileIntA("Lights", "NightLights", 1, ini);
    // Textured headlight beams ("2PLAYERHEADLIGHT1") on the road in front of AI cars:
    // CarRenderInfo::RenderTextureHeadlights (0x7BE170, thiscall, 3 args) draws a textured poly in
    // front of the car (called from RenderFlaresOnCar). HeadlightBeams=0 -> the function returns at once (ret 0xC).
    // flareType 2 "HEADLIGHTGLOW" (see above): HeadlightGlow=0 -> its texture reads as NULL.
    static const BYTE beamCheck[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0 };
    static const BYTE glowCheck[7] = { 0xA1, 0xC4, 0xCF, 0xB4, 0x00, 0xEB, 0x0C };
    if (!GetPrivateProfileIntA("Lights", "HeadlightBeams", 0, ini)) {
        if (!memcmp(g_beamPatch.at, beamCheck, 6)) { g_beamPatch.ok = 1; Patch_Set(&g_beamPatch, 1); Log("lights: AI textured headlight beams disabled (HeadlightBeams=0)\n"); }
        else Log("lights: WARNING RenderTextureHeadlights bytes do not match, AI beams not patched\n");
    } else Log("lights: AI textured headlight beams = original (HeadlightBeams=1)\n");
    if (!GetPrivateProfileIntA("Lights", "HeadlightGlow", 0, ini)) {
        if (!memcmp(g_glowPatch.at, glowCheck, 7)) { g_glowPatch.ok = 1; Patch_Set(&g_glowPatch, 1); Log("lights: AI headlight glow on the road disabled (HeadlightGlow=0)\n"); }
        else Log("lights: WARNING HEADLIGHTGLOW bytes do not match, not patched\n");
    } else Log("lights: AI headlight glow on the road = original (HeadlightGlow=1)\n");
    if (PatchCall(0x00750AAAu, 0x0074D330u, (void*)Flare_World) &&
        PatchCall(0x0074E230u, 0x0074D330u, (void*)Flare_World)) Lights_ReadWorldFlares(ini);
    else Log("lights: WARNING world flare calls do not match, WorldFlares inactive\n");
    Log("lights: NightLights=%d (world lights, AI beams and glow come back at night with [TimeOfDay])\n", g_nightLights);
}
