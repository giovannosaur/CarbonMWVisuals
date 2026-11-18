// Menu.cpp -- in-game settings overlay (Dear ImGui, D3D9) for the time of day
#include "pch.h"
#include "Common.h"
#include <d3d9.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Carbon keeps its IDirect3DDevice9* at [0xAB0ABC]; frames are shown with IDirect3DDevice9::Present
// (vtable 17, e.g. 0x7311B0). The overlay hooks Present (draws last, on top of the HUD) and Reset
// (vtable 16: release/recreate ImGui's D3D objects) in the device's vtable.
#define G_D3DDEVICE 0x00AB0ABCu

typedef HRESULT (WINAPI *PFN_Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (WINAPI *PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static PFN_Present g_origPresent = NULL;
static PFN_Reset   g_origReset = NULL;
static IDirect3DDevice9* g_menuDev = NULL;
static HWND    g_menuWnd = NULL;
static WNDPROC g_origWndProc = NULL;
static int     g_menuKey = 0, g_menuOpen = 0, g_menuReady = 0, g_menuEnable = 1;
static char    g_menuMsg[128] = "";
static DWORD   g_menuMsgTime = 0;

static PFN_Present g_firstPresent = NULL;   // the device's own Present (to spot a restored vtable)
static PFN_Reset   g_firstReset = NULL;

static LRESULT CALLBACK Menu_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_menuReady) {
        // focus changes always reach ImGui, so it does not stay "unfocused" after an Alt+Tab
        int focusMsg = msg == WM_SETFOCUS || msg == WM_KILLFOCUS;
        if ((g_menuOpen || focusMsg) && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) && !focusMsg) return 1;
    }
    return CallWindowProcA(g_origWndProc, hWnd, msg, wParam, lParam);
}

static void Menu_Setup(IDirect3DDevice9* dev)
{
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (FAILED(dev->GetCreationParameters(&cp)) || !cp.hFocusWindow) return;
    g_menuWnd = cp.hFocusWindow;
    float h = 1080.0f;
    IDirect3DSurface9* bb = NULL;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
        D3DSURFACE_DESC d; if (SUCCEEDED(bb->GetDesc(&d))) h = (float)d.Height;
        bb->Release();
    }
    float scale = h / 800.0f; if (scale < 1.0f) scale = 1.0f; if (scale > 3.0f) scale = 3.0f;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;   // no imgui.ini in the game folder
    io.LogFilename = NULL;
    ImFontConfig fc; fc.SizePixels = (float)(int)(13.0f * scale);
    io.Fonts->AddFontDefault(&fc);
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.ScaleAllSizes(scale);
    st.WindowRounding = 6.0f * scale; st.FrameRounding = 4.0f * scale; st.GrabRounding = 4.0f * scale;
    st.Colors[ImGuiCol_WindowBg].w = 0.92f;
    ImGui_ImplWin32_Init(g_menuWnd);
    ImGui_ImplDX9_Init(dev);
    g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_menuWnd, GWLP_WNDPROC, (LONG_PTR)Menu_WndProc);
    g_menuReady = 1;
    Log("menu: ready (window %p, font %.0f px)\n", (void*)g_menuWnd, fc.SizePixels);
}

static void Menu_Status(const char* m)
{
    snprintf(g_menuMsg, sizeof(g_menuMsg), "%s", m);
    g_menuMsgTime = GetTickCount();
}

static void HourText(float h, char* out, size_t n)
{
    int t = (int)(h * 60.0f + 0.5f) % (24 * 60);
    snprintf(out, n, "%02d:%02d", t / 60, t % 60);
}

// Clock controls of one tab (0 = in-game, 1 = menus/garage). Returns nothing; edits *s.
static void Menu_Clock(int which, TodSettings* s)
{
    ImGui::PushID(which);
    char now[16]; HourText(s->hour, now, sizeof(now));
    ImGui::Text("Time %s   sun %.1f deg   night %.2f   shadows %.2f", now, s->sunElevation, s->night, s->shadows);
    if (!s->inGame) ImGui::TextDisabled(which ? "(applies in the menus/garage)" : "(applies in-game)");
    if (TOD_GetSync()) { ImGui::SameLine(); ImGui::TextDisabled("(time synced)"); }
    if (which) {
        bool en = s->enable != 0;
        if (ImGui::Checkbox("Time of day in the menus/garage", &en)) s->enable = en;
        ImGui::SetItemTooltip("Off: the menus/garage use the [FrontEnd] / [FrontEndFx] values as they are.");
    }
    ImGui::BeginDisabled(which && !s->enable);
    const char* modes[] = { "MW daytime only", "MW day / Carbon night (24 h)" };
    ImGui::Combo("Mode", &s->mode, modes, 2);

    ImGui::SeparatorText("Clock");
    float lo = s->mode ? 0.0f : s->minHour, hi = s->mode ? 24.0f : s->maxHour;
    ImGui::SliderFloat("Time (h)", &s->hour, lo, hi, "%.2f");
    ImGui::SetItemTooltip("Saved as the start time.");
    ImGui::SameLine(); ImGui::TextUnformatted(now);
    bool pz = s->paused != 0;
    if (ImGui::Checkbox("Stop the clock", &pz)) s->paused = pz;
    ImGui::SetItemTooltip("Saved: the clock then starts stopped.");
    ImGui::SameLine();
    if (ImGui::Button("Dawn")) s->hour = 6.0f;
    ImGui::SameLine();
    if (ImGui::Button("Noon")) s->hour = 12.0f;
    ImGui::SameLine();
    if (ImGui::Button("Dusk")) s->hour = 17.6f;
    ImGui::SameLine();
    if (ImGui::Button("Night") && s->mode) s->hour = 22.0f;
    ImGui::SliderFloat("Seconds per hour", &s->hourLength, 0.0f, 1000.0f, "%.1f s", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Real seconds per in-game hour (MW: 100). 0 = the clock stands still.");
    ImGui::BeginDisabled(!s->mode);
    ImGui::SliderFloat("Seconds per night hour", &s->nightHourLength, 0.0f, 1000.0f, "%.1f s", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("Used while the sun is down. 0 = same as 'Seconds per hour'.");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(s->mode != 0);
    ImGui::SliderFloat("Min hour", &s->minHour, 6.1f, 17.9f, "%.2f");
    ImGui::SliderFloat("Max hour", &s->maxHour, 6.1f, 17.9f, "%.2f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Weather");
    ImGui::SliderFloat("Overcast", &s->overcast, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("MW weather blend: 0 = clear, 1 = fully overcast.");
    ImGui::SliderFloat("Sunset haze", &s->sunsetHaze, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Share of MW's extra haze towards a low sun (morning/evening).");
    ImGui::EndDisabled();
    ImGui::PopID();
}

static void Menu_Draw(void)
{
    TodSettings s0, s1; TOD_GetSettings(0, &s0); TOD_GetSettings(1, &s1);
    TodSettings o0 = s0, o1 = s1;
    int envOn = 0; char envSet[32]; FE_GetEnv(&envOn, envSet, sizeof(envSet));
    int envOn0 = envOn; char envSet0[32]; memcpy(envSet0, envSet, sizeof(envSet));
    int shOn, shSize, shActive, shAvail; Shadow_GetSettings(&shOn, &shSize, &shActive, &shAvail);
    int shOn0 = shOn, shSize0 = shSize;
    // keep the window usable: if it ended up off screen or squashed (e.g. the game window was
    // minimized while the menu was open), put it back
    static ImVec2 s_pos(40, 40), s_size(0, 0);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float fs = ImGui::GetFontSize();
    int bad = s_size.x > 0 && (s_size.x < fs * 10.0f || s_size.y < fs * 4.0f || s_pos.x > ds.x - fs * 4.0f ||
                                s_pos.y > ds.y - fs * 2.0f || s_pos.x + s_size.x < fs * 4.0f || s_pos.y < -fs);
    ImGui::SetNextWindowPos(ImVec2(40, 40), bad ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(fs * 30.0f, 0), bad ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 20.0f, fs * 6.0f), ImVec2(FLT_MAX, FLT_MAX));
    bool open = true;
    if (!ImGui::Begin("CarbonMWVisuals " CMWV_VERSION, &open, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    s_pos = ImGui::GetWindowPos(); s_size = ImGui::GetWindowSize();

    if (ImGui::BeginTabBar("clocks")) {
        if (ImGui::BeginTabItem("In-game")) { Menu_Clock(0, &s0); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Menus / garage")) {
            Menu_Clock(1, &s1);
            ImGui::SeparatorText("Reflections");
            bool eo = envOn != 0;
            if (ImGui::Checkbox("MW reflection cubemap", &eo)) envOn = eo;
            ImGui::SetItemTooltip("Car reflections in the menus/garage from NFS Most Wanted's garages\n(scripts\\CarbonMWVisuals_FEEnv). Off = Carbon's own.");
            const char* sets[] = { "Auto (follow the Carbon garage)", "QRACE (MW main menu)", "CAREER (MW safehouse)", "CSHOP (MW shop)", "CARLOT (MW car lot)" };
            const char* ids[]  = { "", "QRACE", "CAREER", "CSHOP", "CARLOT" };
            int cur = 0;
            for (int k = 1; k < 5; k++) if (!_stricmp(envSet, ids[k])) cur = k;
            ImGui::BeginDisabled(!envOn);
            if (ImGui::Combo("Set", &cur, sets, 5)) snprintf(envSet, sizeof(envSet), "%s", ids[cur]);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Global")) {
            ImGui::SeparatorText("Time of day");
            bool sy = TOD_GetSync() != 0;
            if (ImGui::Checkbox("Sync time (in-game <-> menus/garage)", &sy)) TOD_SetSync(sy);
            ImGui::SetItemTooltip("One shared time: the menus/garage continue from the in-game time and the other way round.\n"
                                  "Only the time is shared; speed, mode, stop and weather stay separate per tab.\n"
                                  "Needs 'Time of day in the menus/garage' on in the Menus / garage tab.");
            ImGui::SeparatorText("Sun shadows");
            if (!shAvail) ImGui::TextDisabled("Not available (see CarbonMWVisuals.log).");
            ImGui::BeginDisabled(!shAvail);
            bool so = shOn != 0;
            if (ImGui::Checkbox("Enable shadows", &so)) shOn = so;
            static const int sizes[] = { 1024, 2048, 4096 };
            static const char* names[] = { "1024 (low)", "2048 (medium)", "4096 (high)" };
            int si = 1;
            for (int k = 0; k < 3; k++) if (shSize == sizes[k]) si = k;
            if (ImGui::Combo("Shadow quality", &si, names, 3)) shSize = sizes[si];
            ImGui::SetItemTooltip("Shadow map size, for all shadows (world and cars).\nHigher = sharper, more video memory.");
            if (shSize != shActive) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Save, then restart the game to use %d (now %d).", shSize, shActive);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::SeparatorText("Noon sun (both)");
    ImGui::SliderFloat("Elevation (deg)", &s0.noonElevation, 3.0f, 89.0f, "%.1f");
    ImGui::SliderFloat("Direction (deg)", &s0.noonAzimuth, -180.0f, 180.0f, "%.0f");
    ImGui::SetItemTooltip("Where the sun stands at 12:00 (saved as [Sun] Direction). Sunrise and sunset are 90 deg to the sides.");
    s1.noonElevation = s0.noonElevation; s1.noonAzimuth = s0.noonAzimuth;

    ImGui::Separator();
    int save = ImGui::Button("Save to .ini");
    ImGui::SameLine();
    int reload = ImGui::Button("Reload .ini");
    ImGui::SameLine();
    if (ImGui::Button("Close")) open = false;
    if (g_menuMsg[0] && GetTickCount() - g_menuMsgTime < 3000) { ImGui::SameLine(); ImGui::TextDisabled("%s", g_menuMsg); }
    char key[32] = "the menu key";
    UINT sc = MapVirtualKeyA((UINT)g_menuKey, MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextA((LONG)(sc << 16), key, sizeof(key)) <= 0) snprintf(key, sizeof(key), "the menu key");
    ImGui::TextDisabled("%s: show/hide this menu", key);
    ImGui::End();

    if (memcmp(&s1, &o1, sizeof(s1)) != 0) TOD_SetSettings(1, &s1);
    if (memcmp(&s0, &o0, sizeof(s0)) != 0) TOD_SetSettings(0, &s0);
    if (envOn != envOn0 || strcmp(envSet, envSet0)) FE_SetEnv(envOn, envSet);
    if (shOn != shOn0 || shSize != shSize0) Shadow_SetSettings(shOn, shSize);
    if (save) { TOD_SaveIni(); FE_SaveEnv(); Shadow_SaveIni(); Menu_Status("Saved to CarbonMWVisuals.ini"); }
    if (reload) {
        TOD_ReloadIni();
        Shadow_ReloadIni();
        char b[32];
        GetPrivateProfileStringA("FrontEnd", "MWEnvMapSet", "", b, sizeof(b), g_iniPath);
        FE_SetEnv(GetPrivateProfileIntA("FrontEnd", "MWEnvMap", 1, g_iniPath), b);
        Menu_Status("Reloaded from the .ini");
    }
    if (!open) { g_menuOpen = 0; Log("menu: closed (button)\n"); }
}

static void Menu_Render(IDirect3DDevice9* dev)
{
    if (!g_menuReady) Menu_Setup(dev);
    if (!g_menuReady) return;
    // nothing to draw into while the window is minimized or the device is lost / not reset yet
    RECT rc;
    if (IsIconic(g_menuWnd) || !GetClientRect(g_menuWnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) return;
    if (dev->TestCooperativeLevel() != D3D_OK) return;
    IDirect3DSurface9* bb = NULL;
    D3DSURFACE_DESC d; d.Width = 0; d.Height = 0;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) { bb->GetDesc(&d); bb->Release(); }
    if (!d.Width || !d.Height) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    // The back buffer can differ from the window's client size (widescreen fixes, scaling):
    // draw in back buffer pixels and map the cursor accordingly.
    float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    io.DisplaySize = ImVec2((float)d.Width, (float)d.Height);
    // Only while the game window is in front: otherwise clicks in other programs (e.g. after an
    // Alt+Tab, windowed mode) would drag, resize or close the menu.
    int fg = GetForegroundWindow() == g_menuWnd;
    POINT p;
    if (fg && GetCursorPos(&p) && ScreenToClient(g_menuWnd, &p))
        io.AddMousePosEvent((float)p.x * d.Width / cw, (float)p.y * d.Height / ch);
    else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    // buttons polled as well (the game may read the mouse through DirectInput)
    io.AddMouseButtonEvent(0, fg && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, fg && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.MouseDrawCursor = true;
    ImGui::NewFrame();
    Menu_Draw();
    ImGui::EndFrame();
    if (SUCCEEDED(dev->BeginScene())) {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();
    }
}

static HRESULT WINAPI Hook_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r)
{
    if (dev == g_menuDev) {
        if (KeyPressed(g_menuKey)) {
            g_menuOpen = !g_menuOpen;
            if (g_menuOpen && g_menuReady) ImGui::GetIO().AddFocusEvent(GetForegroundWindow() == g_menuWnd);
            Log("menu: %s\n", g_menuOpen ? "open" : "closed");
        }
        if (g_menuOpen) Menu_Render(dev);
    }
    return g_origPresent(dev, a, b, w, r);
}

static HRESULT WINAPI Hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    if (g_menuReady) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_origReset(dev, pp);
    if (g_menuReady && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    if (g_debugMode || FAILED(hr)) Log("menu: device Reset -> 0x%08lX\n", (unsigned long)hr);
    return hr;
}

static void Menu_Hook(IDirect3DDevice9* dev)
{
    void** vt = *(void***)dev;
    DWORD o;
    if (!VirtualProtect(&vt[16], 2 * sizeof(void*), PAGE_READWRITE, &o)) { g_menuEnable = 0; return; }
    if (vt[17] != (void*)Hook_Present) {
        g_origPresent = (PFN_Present)vt[17];
        if (!g_firstPresent) g_firstPresent = g_origPresent;
        vt[17] = (void*)Hook_Present;
    }
    if (vt[16] != (void*)Hook_Reset) {
        g_origReset = (PFN_Reset)vt[16];
        if (!g_firstReset) g_firstReset = g_origReset;
        vt[16] = (void*)Hook_Reset;
    }
    VirtualProtect(&vt[16], 2 * sizeof(void*), o, &o);
}

void Menu_Frame(void)
{
    if (!g_menuEnable) return;
    IDirect3DDevice9* dev = *(IDirect3DDevice9**)G_D3DDEVICE;
    if (!dev) return;
    if (dev != g_menuDev) {
        // first device, or the game made a new one: (re)hook and give ImGui the new device
        if (g_menuReady && g_menuDev) {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplDX9_Init(dev);
            Log("menu: new device %p\n", (void*)dev);
        }
        Menu_Hook(dev);
        g_menuDev = dev;
        Log("menu: Present/Reset hooked (device %p), key vk=0x%02X\n", (void*)dev, g_menuKey);
        return;
    }
    // something (an overlay, a d3d9 wrapper) put the device's own functions back: hook again.
    // Only when the original is back, never on top of someone else's hook (no call loops).
    void** vt = *(void***)dev;
    if (vt[17] == (void*)g_firstPresent || vt[16] == (void*)g_firstReset) {
        Menu_Hook(dev);
        Log("menu: vtable was restored, hooked again\n");
    }
    // the game may put its own window procedure back
    if (g_menuReady && g_menuWnd && (WNDPROC)GetWindowLongPtrA(g_menuWnd, GWLP_WNDPROC) == g_origWndProc)
        SetWindowLongPtrA(g_menuWnd, GWLP_WNDPROC, (LONG_PTR)Menu_WndProc);
}

void Menu_Init(void)
{
    g_menuEnable = GetPrivateProfileIntA("Menu", "Enable", 1, g_iniPath);
    g_menuKey = Cfg_Key("Menu", "Key", "F11");
    if (!g_menuKey) g_menuEnable = 0;
    Log("menu: Enable=%d key vk=0x%02X\n", g_menuEnable, g_menuKey);
}
