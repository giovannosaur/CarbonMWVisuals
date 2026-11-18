// Common.cpp -- shared utilities: log, hotkeys, CALL patching
#include "pch.h"
#include "Common.h"

char  g_gameDir[MAX_PATH];
char  g_iniPath[MAX_PATH];
int   g_debugMode = 0;
static FILE* g_log = NULL;

bool Log_Open(const char* path)
{
    g_log = fopen(path, "w");
    return g_log != NULL;
}

void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fflush(g_log);
}

const char* BufferText(void* buf)
{
    // ID3DXBuffer: vtable[3] = GetBufferPointer
    if (!buf) return "";
    typedef void* (WINAPI *GetPtr)(void*);
    void** vt = *(void***)buf;
    return (const char*)((GetPtr)vt[3])(buf);
}

void ReleaseCom(void* obj)
{
    if (!obj) return;
    typedef ULONG (WINAPI *Rel)(void*);
    void** vt = *(void***)obj;
    ((Rel)vt[2])(obj);
}

int KeyPressed(int vk)
{
    static SHORT prev[256];
    if (vk <= 0 || vk > 255) return 0;
    SHORT now = GetAsyncKeyState(vk);
    int hit = (now & 0x8000) && !(prev[vk] & 0x8000);
    prev[vk] = now;
    return hit;
}

int ParseKey(const char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    char k[32]; int n = 0;
    for (; *s && *s != ' ' && *s != '\t' && *s != ';' && n < 31; s++) k[n++] = (char)toupper((unsigned char)*s);
    k[n] = 0;
    if (!k[0] || !strcmp(k, "NONE")) return 0;
    if (k[0] == 'F' && k[1] >= '1' && k[1] <= '9') {                   // F1..F24
        int f = atoi(k + 1);
        if (f >= 1 && f <= 24) return VK_F1 + f - 1;
    }
    if (!strncmp(k, "NUMPAD", 6) && k[6] >= '0' && k[6] <= '9' && !k[7]) return VK_NUMPAD0 + (k[6] - '0');
    if (!k[1] && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return k[0];   // letter / digit
    static const struct { const char* name; int vk; } named[] = {
        { "INSERT", VK_INSERT }, { "DELETE", VK_DELETE }, { "HOME", VK_HOME }, { "END", VK_END },
        { "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "PAUSE", VK_PAUSE }, { "SCROLLLOCK", VK_SCROLL },
        { "TAB", VK_TAB }, { "SPACE", VK_SPACE }, { "BACKSPACE", VK_BACK },
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) if (!strcmp(k, named[i].name)) return named[i].vk;
    char* end = NULL;
    long v = strtol(k, &end, 0);                                        // "0x72" or "114"
    return (end && !*end && v > 0 && v < 256) ? (int)v : 0;
}

int Cfg_Key(const char* section, const char* key, const char* def)
{
    char b[64];
    GetPrivateProfileStringA(section, key, def, b, sizeof(b), g_iniPath);
    return ParseKey(b);
}

int PatchCall(DWORD site, DWORD expectTarget, void* fn)
{
    BYTE* p = (BYTE*)site;
    if (p[0] != 0xE8 || site + 5 + *(LONG*)(p + 1) != expectTarget) {
        Log("ERROR: call at 0x%08lX does not match, skipped\n", (unsigned long)site);
        return 0;
    }
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(LONG*)(p + 1) = (LONG)((DWORD)fn - (site + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}
