//=============================================================================
//  world.fx  --  NFS Carbon WorldShader with NFS Most Wanted style daytime lighting
//
//  Port of MW v1.3 IDI_WORLD_FX (vs_1_1 + ps_2_0, technique "world" #4) to the
//  interface of Carbon v1.4 IDI_WORLD_FX.
//
//  LOADER: CarbonMWVisuals.asi -> compiled at runtime with the game's own d3dx9_30
//  (the same D3DX version as Carbon's effect pool; safer than the loader of
//  ExtendedCustomization, which uses d3dx9_43). Set EC [CUSTOM_SHADERS] Load = 0.
//
//  REQUIRED: annotation <int shader = N;> on every technique. Carbon (0x72F326)
//  reads the "shader" annotation of the technique in use: if > 0, the engine
//  sets WorldViewProj per object on the effect; if it is missing, the engine uses
//  fixed-function SetTransform -> stale WorldViewProj -> objects move/spin.
//
//  RULE: every parameter (name, type, shared flag, order), sampler and
//  technique "world"/"lowlod" must match Carbon's original world.fx. The engine
//  maps parameters by name hash and picks techniques by name.
//
//  SHADOWS: mwShadowMap/mwShadowTransform/mwShadowParams are set by CarbonMWVisuals.asi every frame
//  (Carbon's own shadow map, switched back on). Technique "world" (SM3) only.
//
//  Other "mw*" parameters are NOT set by the Carbon engine -> their values = defaults in this file.
//  Change the numbers here, then restart the game to tune (or set them from the ASI).
//=============================================================================

//-----------------------------------------------------------------------------
// CARBON INTERFACE (do not change names/types/"shared" flags)
// "shared" = Carbon effect pool parameters: set once by the engine, shared by every effect.
// Without "shared", WorldViewProj etc. are never set -> geometry disappears.
//-----------------------------------------------------------------------------
shared float4x4 WorldViewProj;
shared float4x4 cmWorldViewProj            : REG_cmWorldViewProj;
shared int      ColorWriteMode;
shared int      Cull_Mode;
shared int      BaseMinTextureFilter;
shared int      BaseMagTextureFilter;
shared int      BaseMipTextureFilter;
shared int      BaseTextureFilterParam;
shared int      BaseAddressU;
shared int      BaseAddressV;
shared float    FocalRange;
shared float4   cvFogColour;
shared int      FogColor;
shared float4   cvFogValue;
float4x4 LocalWorld;
float    cfFogEnable                : REG_cfFogEnable;
shared float4   cvVertexPowerBrightness    : REG_cvVertexPowerBrightness;
shared float4x4 cmWorldView                : REG_cmWorldView;
shared float4x4 cmWorldMat                 : REG_cmWorldMat;
shared float4x4 cmWorldMatTranspose        : REG_cmWorldMatTranspose;
float4   cvTextureOffset            : REG_cvTextureOffset;
shared float4   cvDiffuseColour            : REG_cvDiffuseColour;
shared float3   cvLocalEyePos              : REG_cvLocalEyePos;
shared float    cfMipMapBias               : REG_cfMipMapBias;

texture DIFFUSEMAP_TEXTURE;
sampler DIFFUSEMAP_SAMPLER = sampler_state
{
    Texture       = <DIFFUSEMAP_TEXTURE>;
    AddressU      = WRAP;
    AddressV      = WRAP;
    MipFilter     = <BaseMipTextureFilter>;
    MinFilter     = <BaseMinTextureFilter>;
    MagFilter     = <BaseMagTextureFilter>;
    MaxAnisotropy = <BaseTextureFilterParam>;
};
texture MISCMAP1_TEXTURE;
sampler MISCMAP1_SAMPLER = sampler_state
{
    Texture       = <MISCMAP1_TEXTURE>;
    AddressU      = WRAP;
    AddressV      = WRAP;
    MipFilter     = <BaseMipTextureFilter>;
    MinFilter     = <BaseMinTextureFilter>;
    MagFilter     = <BaseMagTextureFilter>;
    MaxAnisotropy = <BaseTextureFilterParam>;
};
texture MISCMAP2_TEXTURE;
sampler MISCMAP2_SAMPLER = sampler_state
{
    Texture       = <MISCMAP2_TEXTURE>;
    AddressU      = WRAP;
    AddressV      = WRAP;
    MipFilter     = <BaseMipTextureFilter>;
    MinFilter     = <BaseMinTextureFilter>;
    MagFilter     = <BaseMagTextureFilter>;
    MaxAnisotropy = <BaseTextureFilterParam>;
};

shared float4   cvHeadlight1Colour;
shared float4   cvHeadlight2Colour;
shared float4   cavWorldHeadlightDirection[2]   : REG_cavWorldHeadlightDirection;
shared float4   cavWorldHeadlightUpDirection[2] : REG_cavWorldHeadlightUpDirection;
shared float4   cavWorldHeadlightPosition[2]    : REG_cavWorldHeadlightPosition;
shared texture  HEADLIGHT_TEXTURE;
shared texture  HEADLIGHT_CLIP_TEXTURE;
float4x4 cmHeadlight1WVP                 : REG_cmHeadlight1WVP;
sampler HEADLIGHT_SAMPLER = sampler_state
{
    Texture = <HEADLIGHT_TEXTURE>;
    AddressU = CLAMP; AddressV = CLAMP;
    MipFilter = NONE; MinFilter = LINEAR; MagFilter = LINEAR;
};
texture  HEADLIGHT_TEXTURE_OLD;
sampler HEADLIGHT_SAMPLER_OLD = sampler_state
{
    Texture = <HEADLIGHT_TEXTURE_OLD>;
    MagFilter = LINEAR; MipFilter = NONE; MinFilter = LINEAR;
    AddressU = CLAMP; AddressV = CLAMP;
};
sampler HEADLIGHT_CLIP_SAMPLER = sampler_state
{
    Texture = <HEADLIGHT_CLIP_TEXTURE>;
    AddressU = CLAMP; AddressV = CLAMP;
    MipFilter = NONE; MinFilter = LINEAR; MagFilter = LINEAR;
};
float    headlight_colour_scale = 0.5;   // original Carbon world.fx default

// Sun direction from the Carbon engine (object space). The engine sets this in
// sky/worldnormalmap/worldreflect; it may not fill it for world.fx. Only used
// when mwSunSource = 1.
shared float4 cvLocalLightVec       : REG_cvLocalLightVec;   // shared float4 in Carbon's sky/worldnormalmap/worldreflect

//-----------------------------------------------------------------------------
// SUN SHADOWS (set by CarbonMWVisuals.asi every frame; shared through the effect pool)
//   mwShadowParams: x = strength (0 = off), y = mode (1 = hw depth compare D24S8,
//                   2 = manual DF16 compare), z = 1/map size
//-----------------------------------------------------------------------------
shared texture  mwShadowMap;
shared float4x4 mwShadowTransform;
shared float4   mwShadowParams = float4(0.0, 1.0, 0.0009765625, 0.0);
shared int      mwShadowFilter = 2;   // 2 = LINEAR (hw 2x2 PCF), 1 = POINT (DF16)
shared float4   mwShadowParams2 = float4(0.0, 1.0, 0.0, 0.0);   // x = darken ambient in shadow, y = PCF radius (texels)

float  mwShadowStrength   = 1.0;      // strength multiplier for this shader (main strength: Strength in CarbonMWVisuals.ini)
float  mwShadowBias       = 0.0004;   // ~0.25 m. Raise it if you see shadow acne, lower it if shadows detach from the base of objects
float  mwShadowSoftness   = 1.0;      // PCF radius multiplier (main radius: Softness in CarbonMWVisuals.ini)

sampler mwShadowSampler = sampler_state
{
    Texture   = <mwShadowMap>;
    MinFilter = <mwShadowFilter>;
    MagFilter = <mwShadowFilter>;
    MipFilter = NONE;
    AddressU  = BORDER;
    AddressV  = BORDER;
    BorderColor = 0xFFFFFFFF;
};

//-----------------------------------------------------------------------------
// MW DAYTIME PARAMETERS (defaults; the Carbon engine never touches these)
// NFS world coordinates: Z = up.
//-----------------------------------------------------------------------------
float  mwDayBlend         = 1.0;    // 0 = original Carbon shading (night), 1 = full MW daytime
float  mwSunSource        = 0.0;    // 0 = use mwSunDirection, 1 = use cvLocalLightVec from the engine
float3 mwSunDirection     = float3(0.35, 0.45, 0.82);    // direction TO the sun (world space)

// == MW TimeOfDayLighting: DiffuseColour (sun) & AmbientColour (w = intensity)
float4 mwDiffuseColour    = float4(0.62, 0.57, 0.48, 1.0);
float4 mwAmbientColour    = float4(0.36, 0.38, 0.42, 1.0);
static const float mwShadowTerm = 1.0;   // SM2/SM1 (lowlod): no shadow map

// == MW atmospheric scattering (Hoffman-Preetham), function 0x6D6CF0 in speed.exe:
//    BR = RayleighColour * (kR*0.01*D + 1e-4), BM = MieColour * (kM*0.01*D + 1e-4)
//    Fog_Const_1 = BR*3/(16pi), Fog_Const_2 = BM/(4pi),
//    Fog_Const_3 = ((1-g)^2, 1+g^2, -2g, InscatterIntensity)
float3 mwBetaRayleigh     = float3(0.000107, 0.000179, 0.000373); // ~ (0.27,0.45,0.94) * 3.97e-4
float3 mwBetaMie          = float3(0.000040, 0.000052, 0.000079); // ~ (0.19,0.25,0.38) * 2.1e-4
float  mwMieG             = -0.50;  // MW convention: negative g = glow towards the sun
float  mwInscatterIntensity = 8.1;
float  mwFogDistanceScale = 1.0;    // MW FOGVALUE (distance * scale, clamped to 2500)
float3 mwInscatterTint    = float3(1.0, 1.0, 1.0);
float  mwSunExtinction    = 0.0;    // 0 = exactly MW (no extinction on the sun), 1 = the sun dims too

// == Carbon vertex colour = baked night lights. In MW, vertex colour = ambient/AO.
float  mwBakedLightMix    = 0.0;    // 0 = drop the night light colours, 1 = use Carbon vertex colours as they are
float  mwBakedAOStrength  = 0.5;    // how strongly the vertex colour luminance is used as AO
float  mwBakedAOScale     = 2.0;    // luminance gain before it becomes AO

float  mwHeadlightScale   = 0.0;   // car headlight beams (player + AI) in daytime: 0 = off, 1 = like at night
float  mwHeadlightOverride = 0.0;  // 1 = mwHeadlightOn decides the beams (time of day clock), 0 = automatic
float  mwHeadlightOn       = 1.0;  // with mwHeadlightOverride = 1: 1 = beams on, 0 = off
float  mwCarbonFogMix     = 0.0;    // 0 = MW scattering only, 1 = add Carbon fog on top

static const float3 LUMINANCE = float3(0.2125, 0.7154, 0.0721);
static const float  PI        = 3.14159265;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
float3 SunDirWorld()
{
    float3 engineL = mul(cvLocalLightVec.xyz, (float3x3)cmWorldMat);
    return normalize(lerp(mwSunDirection, engineL, mwSunSource));
}

// Camera position (object space) from WorldViewProj: the eye point is where clip x, y and w
// all zero. Carbon's original world.fx does not use cvLocalEyePos, so its value can be stale
// (e.g. left over from rendering a car). WorldViewProj is always set for every draw.
float3 EyeObjectSpace()
{
    float4x4 T = transpose(WorldViewProj);   // T[i] = clip coefficients of component i
    float4 a = T[0], b = T[1], c = T[3];
    float3 bc = cross(b.xyz, c.xyz);
    float3 ca = cross(c.xyz, a.xyz);
    float3 ab = cross(a.xyz, b.xyz);
    float  det = dot(a.xyz, bc);
    float3 e = -(a.w * bc + b.w * ca + c.w * ab) / (abs(det) > 1e-20 ? det : 1.0);
    return abs(det) > 1e-20 ? e : cvLocalEyePos;   // orthographic projection -> fallback
}

// Carbon vertex colour: pow(col, power.x) * power.y   (original Carbon world vs)
float3 CarbonVertexColour(float3 c)
{
    return pow(max(c, 1e-5), cvVertexPowerBrightness.x) * cvVertexPowerBrightness.y;
}

// Carbon fog (exactly the original vs_3_0, z = clip-space z). Returns 1 = no fog.
float CarbonFog(float z)
{
    float f = saturate((cvFogValue.x - z) * cvFogValue.y);
    return 1.0 - pow(1.0 - f, cvFogValue.w) * cvFogValue.z * cfFogEnable;
}

// MW daytime lighting (MW world vs, technique 4) -> computed per vertex.
void MWDaylight(float3 wpos, float3 wnrm, float3 vcol,
                out float3 ambE, out float3 sun, out float3 inscat)
{
    float3 L     = SunDirWorld();
    float3 eye   = mul(float4(EyeObjectSpace(), 1.0), cmWorldMat).xyz;
    float3 toEye = eye - wpos;
    float  dist  = length(toEye);
    float3 V     = toEye / max(dist, 1e-3);
    float  cosT  = dot(L, V);

    float  s     = clamp(dist * mwFogDistanceScale, 0.0, 2500.0);
    float3 BrBm  = mwBetaRayleigh + mwBetaMie;
    float3 E     = exp(-s * BrBm);                                   // extinction

    float  g      = mwMieG;
    float  phaseR = 1.0 + cosT * cosT;
    float  phaseM = (1.0 - g) * (1.0 - g) * pow(max(1.0 + g * g - 2.0 * g * cosT, 1e-4), -1.5);
    float3 BrT    = mwBetaRayleigh * (3.0 / (16.0 * PI)) * phaseR;  // Fog_Const_1 * (1+cos^2)
    float3 BmT    = mwBetaMie * (1.0 / (4.0 * PI)) * phaseM;        // Fog_Const_2 * HG
    inscat = saturate((BrT + BmT) / BrBm * (1.0 - E) * mwInscatterIntensity) * mwInscatterTint;

    float  ao  = lerp(1.0, saturate(dot(vcol, LUMINANCE) * mwBakedAOScale), mwBakedAOStrength);
    float3 vc  = lerp(ao.xxx, vcol, mwBakedLightMix);
    ambE = mwAmbientColour.rgb * mwAmbientColour.w * vc * min(E, 1.0);

    float NdotL = saturate(dot(wnrm, L));
    sun = NdotL * mwDiffuseColour.rgb * lerp(1.0, min(E, 1.0), mwSunExtinction);
}

// Sun light factor from the shadow map (1 = in sunlight). wpos = world position.
// Carbon's shadow map = perspective projection from the sun camera (2000 m, ~350 x 260 m around the
// camera). Soft fade at the map edges.
float MWShadowLit(float3 wpos)
{
    float4 sc = mul(float4(wpos, 1.0), mwShadowTransform);
    float3 p  = sc.xyz / max(sc.w, 1e-3);   // w <= 0: behind the shadow projection centre (not part of the map)
    float  z  = p.z - mwShadowBias;
    float  r  = mwShadowParams.z * mwShadowParams2.y * mwShadowSoftness;
    float  manual = saturate(mwShadowParams.y - 1.0);
    float  lit = 0.0;
    float  d;
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.4392, -0.5509) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.2856, -1.4897) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.4906, -1.2961) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(1.1933, -1.0105) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.7667, -0.7604) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.5808, -0.2698) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.0317, 0.0666) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(1.0911, -0.4779) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-1.1774, -0.0025) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.7693, 0.7236) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.4341, 0.1733) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.4107, 0.1123) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(-0.9191, 1.3297) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.2672, 0.5148) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(0.344, 0.9464) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    d = tex2Dproj(mwShadowSampler, float4(p.xy + float2(1.5024, 0.3408) * r, z, 1.0)).r;  lit += lerp(d, (d - z >= 0.0) ? 1.0 : 0.0, manual);
    lit *= 1.0 / 16.0;
    float2 q    = p.xy * (1.0 - p.xy);
    float  outside = (min(q.x, q.y) < 0.0 || p.z > 1.0 || sc.w < 1e-3) ? 1.0 : 0.0;
    float  fade = saturate(1.0 - min(q.x, q.y) * 40.0);   // fade near the map edge (2.5 percent)
    lit = saturate(lit + fade + outside);
    return lit;
}

// lit -> light factors: .x = sun, .y = ambient (Darkness: ambient also dims in shadow, MW style)
float2 MWShadowFactors(float3 wpos)
{
    float lit = MWShadowLit(wpos);
    float on  = saturate(mwShadowParams.x * 1000.0);
    return 1.0 - (1.0 - lit) * float2(saturate(mwShadowStrength * mwShadowParams.x), saturate(mwShadowParams2.x) * on);
}

float MWShadow(float3 wpos)
{
    return MWShadowFactors(wpos).x;
}

// Debug (debug hotkey, mwShadowParams.w = 1): R = shadow x, G = shadow y, B = shadow.
float3 MWShadowDebug(float3 wpos)
{
    float4 sc = mul(float4(wpos, 1.0), mwShadowTransform);
    float3 p  = sc.xyz / max(sc.w, 1e-3);
    float  occl = 1.0 - MWShadowLit(wpos);
    float  raw  = tex2Dproj(mwShadowSampler, float4(p.xy, p.z - mwShadowBias, 1.0)).r;
    float3 a = float3(saturate(p.x), saturate(p.y), saturate(occl + (sc.w < 0.0 ? 0.5 : 0.0)));
    float3 d = lerp(a, raw.xxx, saturate(mwShadowParams.w - 1.0));   // mode 2: raw shadow map value
    return lerp(d, frac(wpos * 0.25), saturate(mwShadowParams.w - 2.0)); // mode 3: 4 m world checkerboard
}

// Carbon headlight cone (exactly the original ps_3_0).
float CarbonHeadlight(float3 wpos, float3 N, float localNz, int i)
{
    float3 d    = cavWorldHeadlightPosition[i].xyz - wpos;
    float  len  = length(d);
    float3 L    = d / max(len, 1e-3);
    float  att  = saturate(1.0 - len * (1.0 / 235.0));
    att *= att;
    float  spot = pow(saturate(dot(L, -cavWorldHeadlightDirection[i].xyz)), 3.7);
    float  a    = min(att * 3.5 * spot, 2.7);
    float  face = saturate(abs(dot(L, N))) * (1.0 + 2.0 * pow(saturate(localNz), 14.0));
    float  u    = 1.0 - abs(dot(L, cavWorldHeadlightUpDirection[i].xyz));
    float  vert = saturate(u * u * u - 0.4) * 2.47;
    return a * face * vert;
}

//-----------------------------------------------------------------------------
// Vertex layout Carbon world: POSITION float3, NORMAL float3, COLOR d3dcolor, TEXCOORD0 float2
//-----------------------------------------------------------------------------
struct VS_IN
{
    float4 pos : POSITION;
    float4 col : COLOR0;
    float4 uv  : TEXCOORD0;
    float3 nrm : NORMAL;
};

struct VS_OUT_HQ
{
    float4 pos    : POSITION;
    float4 uv     : TEXCOORD0;   // xy = uv, w = mip bias
    float4 ambE   : TEXCOORD1;   // rgb = ambient*E, a = vertex alpha
    float3 sun    : TEXCOORD2;
    float3 inscat : TEXCOORD3;
    float3 wpos   : TEXCOORD4;
    float4 wnrm   : TEXCOORD5;   // xyz = world normal, w = local normal.z
    float4 carbon : TEXCOORD6;   // rgb = Carbon vertex colour, a = Carbon fog
};

// Separate PS input: ps_3_0 may not have a POSITION semantic in its input
struct PS_IN_HQ
{
    float4 uv     : TEXCOORD0;
    float4 ambE   : TEXCOORD1;
    float3 sun    : TEXCOORD2;
    float3 inscat : TEXCOORD3;
    float3 wpos   : TEXCOORD4;
    float4 wnrm   : TEXCOORD5;
    float4 carbon : TEXCOORD6;
};

VS_OUT_HQ VS_World(VS_IN i)
{
    VS_OUT_HQ o;
    o.pos = mul(i.pos, WorldViewProj);
    o.uv  = float4(i.uv.xyz + cvTextureOffset.xyz, cfMipMapBias);

    float3 wpos = mul(i.pos, cmWorldMat).xyz;
    float3 wnrm = normalize(mul(i.nrm, (float3x3)cmWorldMat));
    float3 vcol = CarbonVertexColour(i.col.rgb);

    float3 ambE, sun, inscat;
    MWDaylight(wpos, wnrm, vcol, ambE, sun, inscat);

    o.ambE   = float4(ambE, i.col.a);
    o.sun    = sun;
    o.inscat = inscat;
    o.wpos   = wpos;
    o.wnrm   = float4(wnrm, i.nrm.z);
    o.carbon = float4(vcol, CarbonFog(o.pos.z));
    return o;
}

float4 PS_World(PS_IN_HQ i) : COLOR0
{
    float4 tex = tex2Dbias(DIFFUSEMAP_SAMPLER, float4(i.uv.xy, 0.0, i.uv.w));
    float3 N   = normalize(i.wnrm.xyz);

    float hl = min(CarbonHeadlight(i.wpos, N, i.wnrm.w, 0) +
                   CarbonHeadlight(i.wpos, N, i.wnrm.w, 1), 2.0) * cfFogEnable;

    // original Carbon (night)
    float3 night = (i.carbon.rgb + hl * lerp(1.0, mwHeadlightOn, mwHeadlightOverride)) * tex.rgb;
    night = lerp(cvFogColour.rgb, night, i.carbon.a);

    // MW daytime: 2*(tex*ambE + tex*sun*shadow) + inscatter
    float2 shf = MWShadowFactors(i.wpos);   // x = sun, y = ambient (Darkness)
    float3 day = 2.0 * tex.rgb * (i.ambE.rgb * shf.y + i.sun * shf.x)
               + tex.rgb * hl * lerp(mwHeadlightScale, mwHeadlightOn, mwHeadlightOverride)
               + i.inscat;
    day = lerp(day, lerp(cvFogColour.rgb, day, i.carbon.a), mwCarbonFogMix);

    day = lerp(day, MWShadowDebug(i.wpos), saturate(mwShadowParams.w));   // shadow debug view
    return float4(lerp(night, day, mwDayBlend), tex.a * i.ambE.a);
}

//-----------------------------------------------------------------------------
// SM2 version (lowlod & world ps_2_0): no 2-light headlights, no normal in the PS
//-----------------------------------------------------------------------------
struct VS_OUT_LQ
{
    float4 pos    : POSITION;
    float4 uv     : TEXCOORD0;
    float4 light  : TEXCOORD1;   // rgb = 2*(ambE+sun) (day), a = vertex alpha
    float3 inscat : TEXCOORD2;
    float4 carbon : TEXCOORD3;   // rgb = Carbon vertex colour, a = Carbon fog
};

struct PS_IN_LQ
{
    float4 uv     : TEXCOORD0;
    float4 light  : TEXCOORD1;
    float3 inscat : TEXCOORD2;
    float4 carbon : TEXCOORD3;
};

VS_OUT_LQ VS_Simple(VS_IN i)
{
    VS_OUT_LQ o;
    o.pos = mul(i.pos, WorldViewProj);
    o.uv  = float4(i.uv.xyz + cvTextureOffset.xyz, cfMipMapBias);

    float3 wpos = mul(i.pos, cmWorldMat).xyz;
    float3 wnrm = normalize(mul(i.nrm, (float3x3)cmWorldMat));
    float3 vcol = CarbonVertexColour(i.col.rgb);

    float3 ambE, sun, inscat;
    MWDaylight(wpos, wnrm, vcol, ambE, sun, inscat);

    o.light  = float4(2.0 * (ambE + sun * mwShadowTerm), i.col.a);
    o.inscat = inscat;
    o.carbon = float4(vcol, CarbonFog(o.pos.z));
    return o;
}

float4 PS_Simple(PS_IN_LQ i) : COLOR0
{
    float4 tex   = tex2D(DIFFUSEMAP_SAMPLER, i.uv.xy);
    float3 night = lerp(cvFogColour.rgb, i.carbon.rgb * tex.rgb, i.carbon.a);
    float3 day   = tex.rgb * i.light.rgb + i.inscat;
    day = lerp(day, lerp(cvFogColour.rgb, day, i.carbon.a), mwCarbonFogMix);
    return float4(lerp(night, day, mwDayBlend), tex.a * i.light.a);
}

//-----------------------------------------------------------------------------
// world_1_1 version: vs_1_1 + ps_2_0 (d3dx9_43 rejects ps_1_x), colours through COLOR0/COLOR1
//-----------------------------------------------------------------------------
struct VS_OUT_11
{
    float4 pos    : POSITION;
    float4 light  : COLOR0;      // (ambE + sun), a = vertex alpha
    float4 inscat : COLOR1;
    float2 uv     : TEXCOORD0;
};

struct PS_IN_11
{
    float4 light  : COLOR0;
    float4 inscat : COLOR1;
    float2 uv     : TEXCOORD0;
};

VS_OUT_11 VS_11(VS_IN i)
{
    VS_OUT_11 o;
    o.pos = mul(i.pos, WorldViewProj);
    o.uv  = i.uv.xy + cvTextureOffset.xy;

    float3 wpos = mul(i.pos, cmWorldMat).xyz;
    float3 wnrm = normalize(mul(i.nrm, (float3x3)cmWorldMat));
    float3 vcol = CarbonVertexColour(i.col.rgb);

    float3 ambE, sun, inscat;
    MWDaylight(wpos, wnrm, vcol, ambE, sun, inscat);

    o.light  = float4(saturate(ambE + sun * mwShadowTerm), i.col.a);
    o.inscat = float4(inscat, 0.0);
    return o;
}

float4 PS_11(PS_IN_11 i) : COLOR0
{
    float4 tex = tex2D(DIFFUSEMAP_SAMPLER, i.uv);
    return float4(tex.rgb * i.light.rgb * 2.0 + i.inscat.rgb, tex.a * i.light.a);
}

//-----------------------------------------------------------------------------
// Techniques: same count & order as Carbon's original world.fx.
//   Annotation "shader" = same values as the original world.fx (3/2/2/1).
//   The Carbon engine finds techniques by NAME ("world", "lowlod") -> the first one
//   named "world" (SM3) is used. The second one is renamed to world_sm2
//   (originally "world" twice; newer compilers may reject duplicate names).
//   [0] world (SM3)   [1] lowlod   [2] world_sm2 (SM2)   [3] world_1_1
//-----------------------------------------------------------------------------
technique world < int shader = 3; >
{
    pass p0
    {
        VertexShader = compile vs_3_0 VS_World();
        PixelShader  = compile ps_3_0 PS_World();
    }
}

technique lowlod < int shader = 2; >
{
    pass p0
    {
        VertexShader = compile vs_2_0 VS_Simple();
        PixelShader  = compile ps_2_0 PS_Simple();
    }
}

technique world_sm2 < int shader = 2; >
{
    pass p0
    {
        FogEnable    = false;
        VertexShader = compile vs_2_0 VS_Simple();
        PixelShader  = compile ps_2_0 PS_Simple();
    }
}

technique world_1_1 < int shader = 1; >
{
    pass p0
    {
        FogEnable        = false;
        ColorWriteEnable = <ColorWriteMode>;
        ZFunc            = LESSEQUAL;
        CullMode         = <Cull_Mode>;
        VertexShader     = compile vs_2_0 VS_11();   // vs_1_1 allows 128 instructions max, this needs ~181
        PixelShader      = compile ps_2_0 PS_11();   // d3dx9_43 can no longer compile ps_1_x
    }
}
