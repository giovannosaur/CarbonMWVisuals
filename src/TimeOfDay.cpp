// TimeOfDay.cpp -- NFS Most Wanted style time of day: moving sun + MW timeofdaylighting blend
#include "pch.h"
#include "Common.h"

// --- How MW does it (speed.exe) --------------------------------------------------------------
// TimeOfDay object [0x9B392C]: +8 time 0..1, ticked every frame by 0x769360:
//   time += realDt / 60 * 0.05 * direction, bouncing between 0.05 and 0.95 (~18 min per sweep).
//   Sun: h = (time - 0.5) * pi; z = cos(b) cos(h) -> noon at time 0.5 (b = 0.5: 61 degrees),
//   ~8 degrees at the limits. (MW's horizontal formula is skewed; here a proper rotation is used.)
// Lighting 0x769580: four timeofdaylighting sets (class 0x399ED882, Visual Treatment on group):
//   midday clear 0x3DCFA673, low sun clear 0xB902CFAD, midday overcast 0x59C373C3,
//   low sun overcast 0xD835D0C8. lerp(clear, overcast, overcast factor) for midday and low sun,
//   then lerp(midday, lowsun, low) with low = 1 - (clamp(sunZ, 0.25, 0.5) - 0.25) * 4.
// Shader constants (0x6C6300 / 0x6C6450 / 0x6D6CF0):
//   DIFFUSECOLOUR = sun, AMBIENTCOLOUR = ambient, SPECULARCOLOUR = spec,
//   Rayleigh = RayleighColour * (kR * 0.01 * 0.0099 + 1e-4), Mie = MieColour * (kM * 0.01 * 0.0099 + 1e-4),
//   Fog_Const_3 = HG g and inscatter intensity, FOGVALUE = fog distance scale.
//
// --- How it is applied here -------------------------------------------------------------------
// The fx files hold the user's tuned noon look. Every frame in-game, each parameter is set to
// its fx value scaled by (MW value now / MW midday clear value), so noon with Overcast=0 looks
// exactly like the fx files and the rest of the day changes the way MW's data changes.
// The noon sun direction is mwSunDirection ([Sun] Direction or the fx default).
// The menus/garage (FE) have their own clock ([FrontEndTimeOfDay]). There the noon look is the FE
// look: fx values with [FrontEnd] / [FrontEndFx] applied (FE_ValueFor). While loading, or with the FE
// clock off, the plain fx values are put back once, so [FrontEnd] / [FrontEndFx] work as before.
//
// --- Night (Mode=1) ---------------------------------------------------------------------------
// The sun keeps going below the horizon. Between +4 and -6 degrees of sun elevation every port
// effect's mwDayBlend goes from its fx value to 0 = Carbon's own night shading (baked lights,
// headlight beams, original visual treatment LUT/bloom/desaturation). The sun light fades out
// before the horizon, the sun shadows fade out between 6 and 0 degrees and the world lights come
// back (Lights_SetNight). Car headlight beams (shader beams + AI beams/glow) are switched by the clock
// instead: on at HeadlightsOn, off at HeadlightsOff (mwHeadlightOverride/mwHeadlightOn, Lights_SetHeadlights).

struct TodSet {
    float sun[3], amb[3], mie[3], sky[3], ray[3];
    float fogValue, kM, kR, g, inscatter;
};
// MW GLOBAL\attributes.bin (layout +0x10 sun, +0x20 ambient, +0x30 Mie colour, +0x40 sky tint,
// +0x50 FOGVALUE, +0x54 kM, +0x58 kR; attributes 0x1D76EA96 Rayleigh colour, 0x2EF8A8BF g, 0x00756239 intensity)
static const TodSet kMidClear = { {0.60f, 0.48f, 0.22f}, {0.19f, 0.24f, 0.32f}, {0.18f, 0.24f, 0.37f},
                                  {0.86f, 0.95f, 1.00f}, {0.27f, 0.46f, 1.00f}, 4.0f, 0.1f, 1.0f, 0.0f, 10.0f };
static const TodSet kLowClear = { {0.73f, 0.48f, 0.05f}, {0.15f, 0.19f, 0.36f}, {0.61f, 0.29f, 0.04f},
                                  {1.00f, 0.91f, 0.64f}, {0.27f, 0.46f, 0.95f}, 4.0f, 3.0f, 0.001f, -0.3f, 3.0f };
static const TodSet kMidOver  = { {0.03f, 0.04f, 0.05f}, {0.23f, 0.27f, 0.32f}, {0.07f, 0.15f, 0.22f},
                                  {0.53f, 0.56f, 0.57f}, {0.27f, 0.44f, 0.95f}, 10.0f, 1.0f, 2.0f, -0.15f, 3.0f };
static const TodSet kLowOver  = { {0.34f, 0.22f, 0.02f}, {0.20f, 0.23f, 0.27f}, {0.17f, 0.06f, 0.00f},
                                  {1.00f, 0.90f, 0.63f}, {0.26f, 0.45f, 1.00f}, 10.0f, 3.0f, 0.01f, -0.3f, 2.0f };

static void LerpSet(TodSet* o, const TodSet* a, const TodSet* b, float t)
{
    const float* pa = (const float*)a; const float* pb = (const float*)b; float* po = (float*)o;
    for (size_t i = 0; i < sizeof(TodSet) / sizeof(float); i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}
static float Scatter(float k) { return k * 0.01f * 0.0099f + 1e-4f; }   // 0x6D6CF0
static float Ratio(float now, float ref) { return ref > 1e-6f ? now / ref : 1.0f; }
static float Smooth(float e0, float e1, float x)   // smoothstep, e0 > e1 allowed
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// --- managed shader parameters ---------------------------------------------------------------
enum { P_SUNDIR, P_DIFF, P_AMB, P_RAY, P_MIE, P_G, P_INSC, P_FOGD, P_SKYH, P_SKYZ, P_GLOW, P_DISK, P_DAYBLEND,
       P_HLOVR, P_HLON, P_N };
static const char* const kParam[P_N] = {
    "mwSunDirection", "mwDiffuseColour", "mwAmbientColour", "mwBetaRayleigh", "mwBetaMie", "mwMieG",
    "mwInscatterIntensity", "mwFogDistanceScale", "mwSkyHorizonColour", "mwSkyZenithColour",
    "mwSunGlowColour", "mwSunDiskColour", "mwDayBlend", "mwHeadlightOverride", "mwHeadlightOn" };
static const int kIsFloat[P_N] = { 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1 };

#define TOD_MAXFX 48
struct TodFx { void* fx; void* h[P_N]; float base[P_N][4]; float feBase[P_N][4]; };
static TodFx g_tod[TOD_MAXFX]; static int g_todNum = 0;

// One clock for in-game ([TimeOfDay], mode from [Main]) and one for the FE ([FrontEndTimeOfDay]).
struct TodClock {
    const char* sec;
    int    enable, mode, paused, dir, lastLogHour;
    double hour, start, hourLen, nightLen, minH, maxH;
    float  overcast, haze;
    float  sunZ, elev, night, low, shadows;   // last applied (menu status)
};
static TodClock g_clk[2] = {
    { "TimeOfDay",         1, 1, 0, 1, -1, 12.0, 12.0, 100.0, 0.0, 6.6, 17.4, 0.0f, 0.35f, 1.0f, 90.0f, 0.0f, 0.0f, 1.0f },
    { "FrontEndTimeOfDay", 0, 1, 1, 1, -1, 12.0, 12.0, 100.0, 0.0, 6.6, 17.4, 0.0f, 0.35f, 1.0f, 90.0f, 0.0f, 0.0f, 1.0f } };
float         g_todShadowScale = 1.0f;
static double g_todHlOn = 18.0 + 4.0 / 60.0, g_todHlOff = 6.0 + 4.0 / 60.0;   // car headlight beams (clock)
static int    g_todHlState = 0;
static int    g_todState = 0;            // GameFlowState
static int    g_todActive = -1;          // clock applied last frame (-1 = none, fx values)
static int    g_todFeGen = -1;           // FE settings generation feBase was built from
static int    g_todArcReady = 0;
static float  g_todA = 0.0f, g_todB = 0.5f;   // noon azimuth (from +Y towards +X) and zenith angle
static LARGE_INTEGER g_todLastTick, g_todFreq;
static double g_todDt = 0.0;
static int    g_todSync = 0;             // [Main] SyncTime: in-game and FE share the time (only the time)

static void Tod_SetArc(const float* noon)
{
    float l = (float)sqrt(noon[0] * noon[0] + noon[1] * noon[1] + noon[2] * noon[2]);
    if (l < 1e-4f) return;
    float x = noon[0] / l, y = noon[1] / l, z = noon[2] / l;
    if (z > 1.0f) z = 1.0f;
    if (z < 0.05f) z = 0.05f;   // noon sun must be above the horizon
    g_todB = (float)acos(z);
    g_todA = (float)atan2(x, y);
    g_todArcReady = 1;
    Log("TOD: noon sun (%.3f %.3f %.3f) -> elevation %.1f deg\n", x, y, z, 90.0 - g_todB * 57.29578);
}

void TOD_OnEffect(const char* res, void* fx)
{
    if (!fx || g_todNum >= TOD_MAXFX) return;
    TodFx* t = &g_tod[g_todNum];
    memset(t, 0, sizeof(*t));
    int any = 0;
    for (int i = 0; i < P_N; i++) {
        t->h[i] = FxParam(fx, kParam[i]);
        if (!t->h[i]) continue;
        any = 1;
        if (kIsFloat[i]) ((FX_GetFloat)(*(void***)fx)[31])(fx, t->h[i], &t->base[i][0]);
        else ((FX_GetVector)(*(void***)fx)[35])(fx, t->h[i], t->base[i]);
    }
    if (!any) return;
    typedef ULONG (WINAPI *AddRefFn)(void*);
    ((AddRefFn)(*(void***)fx)[1])(fx);
    t->fx = fx;
    g_todNum++;
    g_todFeGen = -1;
    if (t->h[P_SUNDIR] && (!g_todArcReady || (res && !strcmp(res, "IDI_WORLD_FX"))))
        Tod_SetArc(t->base[P_SUNDIR]);
}

static void Tod_Restore(int fe)
{
    for (int e = 0; e < g_todNum; e++) {
        TodFx* t = &g_tod[e];
        for (int i = 0; i < P_N; i++) {
            if (!t->h[i]) continue;
            const float* v = fe ? t->feBase[i] : t->base[i];
            if (kIsFloat[i]) ((FX_SetFloat)(*(void***)t->fx)[30])(t->fx, t->h[i], v[0]);
            else ((FX_SetVector)(*(void***)t->fx)[34])(t->fx, t->h[i], v);
        }
    }
}

// FE noon look = fx values with the FE settings applied
static void Tod_BuildFeBase(void)
{
    int gen = FE_Generation();
    if (gen == g_todFeGen) return;
    g_todFeGen = gen;
    for (int e = 0; e < g_todNum; e++) {
        TodFx* t = &g_tod[e];
        for (int i = 0; i < P_N; i++) {
            memcpy(t->feBase[i], t->base[i], sizeof(t->feBase[i]));
            if (t->h[i]) FE_ValueFor(t->fx, kParam[i], t->feBase[i]);
        }
    }
}

static void Tod_Apply(TodClock* c, int fe)
{
    // sun direction for the current hour
    float hAng = (float)((c->hour - 12.0) / 12.0 * 3.14159265358979);
    float u = (float)sin(hAng), v = (float)(sin(g_todB) * cos(hAng)), z = (float)(cos(g_todB) * cos(hAng));
    float ca = (float)cos(g_todA), sa = (float)sin(g_todA);
    float sun[4] = { ca * u + sa * v, -sa * u + ca * v, z, 0.0f };
    float sz = z < 0.25f ? 0.25f : (z > 0.5f ? 0.5f : z);
    float low = 1.0f - (sz - 0.25f) * 4.0f;
    float elev = (float)(asin(z) * 57.29578);
    float night = Smooth(4.0f, -6.0f, elev);      // 0 = day, 1 = Carbon night
    float sunFade = Smooth(-2.0f, 4.0f, elev);    // sun light gone before the horizon
    g_todShadowScale = Smooth(0.0f, 6.0f, elev);
    c->sunZ = z; c->elev = elev; c->night = night; c->low = low; c->shadows = g_todShadowScale;
    Lights_SetNight(night);
    // car headlight beams: switched by the clock, not faded with the night blend
    int hlOn = g_todHlOn > g_todHlOff ? (c->hour >= g_todHlOn || c->hour < g_todHlOff)
                                      : (c->hour >= g_todHlOn && c->hour < g_todHlOff);
    if (g_todHlOn == g_todHlOff) hlOn = 0;
    if (g_debugMode && hlOn != g_todHlState) Log("TOD: car headlight beams %s at %.2f h\n", hlOn ? "on" : "off", c->hour);
    g_todHlState = hlOn;
    Lights_SetHeadlights(hlOn);

    TodSet mid, lo, m;
    LerpSet(&mid, &kMidClear, &kMidOver, c->overcast);
    LerpSet(&lo, &kLowClear, &kLowOver, c->overcast);
    LerpSet(&m, &mid, &lo, low);
    const TodSet& r = kMidClear;

    float fDiff[3], fAmb[3], fRay[3], fMie[3], fSky[3];
    for (int k = 0; k < 3; k++) {
        fDiff[k] = Ratio(m.sun[k], r.sun[k]) * sunFade;
        fAmb[k]  = Ratio(m.amb[k], r.amb[k]);
        fRay[k]  = Ratio(m.ray[k] * Scatter(m.kR), r.ray[k] * Scatter(r.kR));
        // MW's low-sun Mie is ~12x midday (red). On top of fx values tuned for a strong noon haze that
        // washes distant objects out towards the sun -> only SunsetHaze of the increase is used.
        fMie[k]  = 1.0f + (Ratio(m.mie[k] * Scatter(m.kM), r.mie[k] * Scatter(r.kM)) - 1.0f) * c->haze;
        fSky[k]  = Ratio(m.sky[k], r.sky[k]);
    }
    // MW's g goes 0 -> -0.3 at low sun; added to an fx g of -0.5 the forward peak gets ~20x stronger
    // (sun glare through distant objects), so g stays at the fx value.
    float fIns = Ratio(m.inscatter, r.inscatter), fFog = Ratio(m.fogValue, r.fogValue);

    for (int e = 0; e < g_todNum; e++) {
        TodFx* t = &g_tod[e];
        for (int i = 0; i < P_N; i++) {
            if (!t->h[i]) continue;
            const float* b = fe ? t->feBase[i] : t->base[i];
            float o[4] = { b[0], b[1], b[2], b[3] };
            const float* f = NULL;
            switch (i) {
            case P_SUNDIR: o[0] = sun[0]; o[1] = sun[1]; o[2] = sun[2]; o[3] = 0.0f; break;
            case P_DIFF: case P_GLOW: case P_DISK: f = fDiff; break;
            case P_AMB: case P_SKYZ: f = fAmb; break;
            case P_RAY:  f = fRay; break;
            case P_MIE:  f = fMie; break;
            case P_SKYH: f = fSky; break;
            case P_INSC: o[0] = b[0] * fIns; break;
            case P_FOGD: o[0] = b[0] * fFog; break;
            case P_DAYBLEND: o[0] = b[0] * (1.0f - night); break;
            case P_HLOVR: o[0] = 1.0f; break;
            case P_HLON:  o[0] = hlOn ? 1.0f : 0.0f; break;
            }
            if (f) { o[0] = b[0] * f[0]; o[1] = b[1] * f[1]; o[2] = b[2] * f[2]; }
            if (kIsFloat[i]) ((FX_SetFloat)(*(void***)t->fx)[30])(t->fx, t->h[i], o[0]);
            else ((FX_SetVector)(*(void***)t->fx)[34])(t->fx, t->h[i], o);
        }
    }
    if (g_debugMode && (int)c->hour != c->lastLogHour) {
        c->lastLogHour = (int)c->hour;
        Log("TOD%s: %02d:%02d sun (%.3f %.3f %.3f) elevation %.1f deg, low sun %.2f, night %.2f, shadows %.2f, "
            "overcast %.2f, sun x(%.2f %.2f %.2f) mie x(%.2f %.2f %.2f)\n", fe ? " (FE)" : "",
            (int)c->hour, (int)((c->hour - (int)c->hour) * 60.0), sun[0], sun[1], sun[2],
            elev, low, night, g_todShadowScale, c->overcast, fDiff[0], fDiff[1], fDiff[2], fMie[0], fMie[1], fMie[2]);
    }
}

static void Tod_Advance(TodClock* c, double dt)
{
    double len = (c->sunZ < 0.0f && c->nightLen > 0.0) ? c->nightLen : c->hourLen;
    if (len <= 0.0 || c->paused) return;
    if (c->mode == 1) {
        c->hour += dt / len;
        while (c->hour >= 24.0) c->hour -= 24.0;
    } else if (c->maxH > c->minH) {
        c->hour += dt / len * c->dir;
        if (c->hour >= c->maxH) { c->hour = c->maxH; c->dir = -1; }
        if (c->hour <= c->minH) { c->hour = c->minH; c->dir = 1; }
    }
}

static void Tod_ClampClock(TodClock* c);

static int Tod_WantedClock(void)
{
    if (!g_todArcReady || !g_todNum) return -1;
    if (g_todState == 6) return 0;
    if (g_todState == 3 && g_clk[1].enable) return 1;
    return -1;
}

// Per frame, before FE_Update: put the fx values back when the clock in use changes (or stops),
// so the FE module always starts from (and caches) the plain fx values.
void TOD_Update(void)
{
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double dt = g_todFreq.QuadPart ? (double)(now.QuadPart - g_todLastTick.QuadPart) / (double)g_todFreq.QuadPart : 0.0;
    g_todLastTick = now;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;   // hitches, loading
    g_todDt = dt;
    g_todState = *(int*)0x00A99BBCu;   // GameFlowState
    int want = Tod_WantedClock();
    // FE clock switched off while in the FE: back to the FE look (the FE module has already applied
    // it and will not again); every other change: back to the plain fx values.
    if (want != g_todActive && g_todActive >= 0) Tod_Restore(g_todActive == 1 && g_todState == 3);
    if (want < 0) {
        g_todActive = -1;
        g_todShadowScale = 1.0f;
        Lights_SetNight(0.0f);
        Lights_SetHeadlights(0);
    }
}

// Per frame, after FE_Update: run and apply the clock in use.
void TOD_UpdatePost(void)
{
    int want = Tod_WantedClock();
    g_todActive = want;
    if (want < 0) return;
    TodClock* c = &g_clk[want];
    if (want == 1) Tod_BuildFeBase();
    Tod_Advance(c, g_todDt);   // with the active clock's own speed / stop / mode
    if (g_todSync) {
        // SyncTime: the clock in use carries the time; the other one continues from it later
        // (its own speed, mode and weather). A MW-daytime-only clock keeps it within Min/MaxHour.
        TodClock* o = &g_clk[1 - want];
        o->hour = c->hour;
        Tod_ClampClock(o);
    }
    Tod_Apply(c, want == 1);
}

static double ReadIniDouble(const char* sec, const char* key, const char* def, const char* ini)
{
    char b[64];
    GetPrivateProfileStringA(sec, key, def, b, sizeof(b), ini);
    return atof(b);
}

// "18:04" or "18.07" -> hours
static double ReadIniHour(const char* key, const char* def, const char* ini)
{
    char b[64];
    GetPrivateProfileStringA("TimeOfDay", key, def, b, sizeof(b), ini);
    int h = 0, m = 0;
    double v = strchr(b, ':') && sscanf(b, "%d:%d", &h, &m) == 2 ? h + m / 60.0 : atof(b);
    while (v < 0.0) v += 24.0;
    while (v >= 24.0) v -= 24.0;
    return v;
}

static void Tod_ClampClock(TodClock* c)
{
    if (c->haze < 0.0f) c->haze = 0.0f;
    if (c->haze > 1.0f) c->haze = 1.0f;
    if (c->overcast < 0.0f) c->overcast = 0.0f;
    if (c->overcast > 1.0f) c->overcast = 1.0f;
    if (c->hourLen < 0.0) c->hourLen = 0.0;
    if (c->nightLen < 0.0) c->nightLen = 0.0;
    if (c->minH < 6.1) c->minH = 6.1;      // MW mode keeps the sun above the horizon
    if (c->maxH > 17.9) c->maxH = 17.9;
    if (c->maxH < c->minH) c->maxH = c->minH;
    if (c->mode == 1) {
        while (c->hour < 0.0) c->hour += 24.0;
        while (c->hour >= 24.0) c->hour -= 24.0;
    } else {
        if (c->hour < c->minH) c->hour = c->minH;
        if (c->hour > c->maxH) c->hour = c->maxH;
    }
}

static void Tod_ReadClock(TodClock* c, int fe, int resetTime, const char* ini)
{
    const char* s = c->sec;
    if (fe) {
        c->enable = GetPrivateProfileIntA(s, "Enable", 0, ini) ? 1 : 0;
        c->mode   = GetPrivateProfileIntA(s, "Mode", 1, ini) ? 1 : 0;
    } else {
        c->enable = 1;   // the whole mod is switched by [Main] Enabled
        // [Main] Mode (older .ini files: [TimeOfDay] Mode)
        c->mode = GetPrivateProfileIntA("Main", "Mode", GetPrivateProfileIntA("TimeOfDay", "Mode", 1, ini), ini) ? 1 : 0;
    }
    c->start    = ReadIniDouble(s, "StartHour", "12", ini);
    c->paused   = GetPrivateProfileIntA(s, "StopClock", fe ? 1 : 0, ini) ? 1 : 0;
    c->hourLen  = ReadIniDouble(s, "HourLength", "100", ini);
    c->nightLen = ReadIniDouble(s, "NightHourLength", "0", ini);
    c->minH     = ReadIniDouble(s, "MinHour", "6.6", ini);
    c->maxH     = ReadIniDouble(s, "MaxHour", "17.4", ini);
    c->overcast = (float)ReadIniDouble(s, "Overcast", "0", ini);
    c->haze     = (float)ReadIniDouble(s, "SunsetHaze", "0.35", ini);
    if (resetTime) { c->hour = c->start; c->dir = 1; }
    Tod_ClampClock(c);
    c->lastLogHour = -1;
    Log("TOD [%s]: Enable=%d Mode=%d (%s) hour=%.2f StopClock=%d MinHour=%.2f MaxHour=%.2f HourLength=%.1f s "
        "NightHourLength=%.1f s Overcast=%.2f SunsetHaze=%.2f\n", s, c->enable, c->mode,
        c->mode ? "MW day / Carbon night, 24 h" : "MW daytime only", c->hour, c->paused, c->minH, c->maxH,
        c->hourLen, c->nightLen, c->overcast, c->haze);
}

void TOD_ReadIni(int resetTime)
{
    const char* ini = g_iniPath;
    Tod_ReadClock(&g_clk[0], 0, resetTime, ini);
    Tod_ReadClock(&g_clk[1], 1, resetTime, ini);
    g_todHlOn  = ReadIniHour("HeadlightsOn", "18:04", ini);
    g_todHlOff = ReadIniHour("HeadlightsOff", "06:04", ini);
    g_todSync  = GetPrivateProfileIntA("Main", "SyncTime", 0, ini) ? 1 : 0;
    Log("TOD: car headlight beams %.2f -> %.2f, SyncTime=%d\n", g_todHlOn, g_todHlOff, g_todSync);
}

void TOD_Init(void)
{
    QueryPerformanceFrequency(&g_todFreq);
    QueryPerformanceCounter(&g_todLastTick);
    TOD_ReadIni(1);
}

// --- settings menu (Menu.cpp) ----------------------------------------------------------------
void TOD_GetSettings(int which, TodSettings* o)
{
    TodClock* c = &g_clk[which ? 1 : 0];
    o->enable = c->enable; o->mode = c->mode; o->paused = c->paused;
    o->hour = (float)c->hour; o->startHour = (float)c->start;
    o->hourLength = (float)c->hourLen; o->nightHourLength = (float)c->nightLen;
    o->minHour = (float)c->minH; o->maxHour = (float)c->maxH;
    o->overcast = c->overcast; o->sunsetHaze = c->haze;
    o->noonElevation = 90.0f - g_todB * 57.29578f;
    o->noonAzimuth = g_todA * 57.29578f;
    o->headlightsOn = (float)g_todHlOn; o->headlightsOff = (float)g_todHlOff;
    o->sunElevation = c->elev; o->night = c->night; o->lowSun = c->low;
    o->shadows = c->shadows; o->inGame = g_todActive == (which ? 1 : 0); o->ready = g_todArcReady;
}

void TOD_SetSettings(int which, const TodSettings* in)
{
    TodClock* c = &g_clk[which ? 1 : 0];
    if (which) c->enable = in->enable ? 1 : 0;
    c->mode = in->mode ? 1 : 0;
    c->paused = in->paused ? 1 : 0;
    c->start = in->startHour;
    c->hourLen = in->hourLength;
    c->nightLen = in->nightHourLength;
    c->minH = in->minHour; c->maxH = in->maxHour;
    c->overcast = in->overcast; c->haze = in->sunsetHaze;
    if (g_todSync && in->hour != (float)c->hour) {   // time moved in the menu: both clocks
        TodClock* o = &g_clk[which ? 0 : 1];
        o->hour = in->hour;
        Tod_ClampClock(o);
    }
    c->hour = in->hour;
    Tod_ClampClock(c);
    float el = in->noonElevation < 3.0f ? 3.0f : (in->noonElevation > 89.0f ? 89.0f : in->noonElevation);
    g_todB = (90.0f - el) / 57.29578f;
    g_todA = in->noonAzimuth / 57.29578f;
}

static void WriteIniFloat(const char* sec, const char* key, double v, const char* fmt)
{
    char b[64]; snprintf(b, sizeof(b), fmt, v);
    WritePrivateProfileStringA(sec, key, b, g_iniPath);
}

static void Tod_SaveClock(TodClock* c, int fe)
{
    const char* s = c->sec;
    if (fe) {
        WritePrivateProfileStringA(s, "Enable", c->enable ? "1" : "0", g_iniPath);
        WritePrivateProfileStringA(s, "Mode", c->mode ? "1" : "0", g_iniPath);
    } else {
        WritePrivateProfileStringA("Main", "Mode", c->mode ? "1" : "0", g_iniPath);
        WritePrivateProfileStringA(s, "Enable", NULL, g_iniPath);   // old keys, now in [Main]
        WritePrivateProfileStringA(s, "Mode", NULL, g_iniPath);
    }
    c->start = c->hour;   // the current time becomes the start time
    WriteIniFloat(s, "StartHour", c->start, "%.2f");
    WritePrivateProfileStringA(s, "StopClock", c->paused ? "1" : "0", g_iniPath);
    WriteIniFloat(s, "HourLength", c->hourLen, "%.1f");
    WriteIniFloat(s, "NightHourLength", c->nightLen, "%.1f");
    WriteIniFloat(s, "MinHour", c->minH, "%.2f");
    WriteIniFloat(s, "MaxHour", c->maxH, "%.2f");
    WriteIniFloat(s, "Overcast", c->overcast, "%.2f");
    WriteIniFloat(s, "SunsetHaze", c->haze, "%.2f");
}

void TOD_SaveIni(void)
{
    Tod_SaveClock(&g_clk[0], 0);
    Tod_SaveClock(&g_clk[1], 1);
    WritePrivateProfileStringA("Main", "SyncTime", g_todSync ? "1" : "0", g_iniPath);
    // HeadlightsOn / HeadlightsOff are .ini-only (not in the menu, not written here)
    // noon sun -> [Sun] Direction (direction TO the sun, z = up)
    char b[96];
    snprintf(b, sizeof(b), " %.3f, %.3f, %.3f", sin(g_todA) * sin(g_todB), cos(g_todA) * sin(g_todB), cos(g_todB));
    WritePrivateProfileStringA("Sun", "Direction", b, g_iniPath);
    Log("TOD: settings saved to the .ini (menu)\n");
}

void TOD_ReloadIni(void)
{
    TOD_ReadIni(1);
    // [Sun] Direction -> noon arc (the fx base value only changes after a restart)
    char buf[128]; float d[3];
    GetPrivateProfileStringA("Sun", "Direction", "", buf, sizeof(buf), g_iniPath);
    if (sscanf(buf, "%f , %f , %f", &d[0], &d[1], &d[2]) == 3) Tod_SetArc(d);
}

int  TOD_GetSync(void) { return g_todSync; }
void TOD_SetSync(int on)
{
    on = on ? 1 : 0;
    if (on != g_todSync) Log("TOD: SyncTime=%d (menu)\n", on);
    g_todSync = on;
}
