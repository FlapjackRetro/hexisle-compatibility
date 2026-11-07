/* hexfix d3d9.dll proxy for Cosmic Osmo's Hex Isle (Plasma 3.0)
 * Optional frame-rate limiter and VSync override. Defaults = vanilla passthrough.
 * Freestanding: no CRT, no Windows SDK headers. Build: see build.sh */
typedef unsigned int  u32; typedef int i32; typedef unsigned short u16;
typedef unsigned long long u64; typedef long long i64;
typedef void *HANDLE, *HMODULE, *PVOID; typedef i32 BOOL; typedef i32 HRESULT;
typedef u16 WCHAR;
#define WINAPI __stdcall
#define IMP __declspec(dllimport)
IMP HMODULE WINAPI LoadLibraryW(const WCHAR*);
IMP PVOID   WINAPI GetProcAddress(HMODULE, const char*);
IMP u32     WINAPI GetSystemDirectoryW(WCHAR*, u32);
IMP u32     WINAPI GetModuleFileNameW(HMODULE, WCHAR*, u32);
IMP BOOL    WINAPI VirtualProtect(PVOID, u32, u32, u32*);
IMP BOOL    WINAPI QueryPerformanceCounter(i64*);
IMP BOOL    WINAPI QueryPerformanceFrequency(i64*);
IMP HANDLE  WINAPI CreateWaitableTimerExW(PVOID, const WCHAR*, u32, u32);
IMP BOOL    WINAPI SetWaitableTimer(HANDLE, const i64*, i32, PVOID, PVOID, BOOL);
IMP u32     WINAPI WaitForSingleObject(HANDLE, u32);
IMP u32     WINAPI GetPrivateProfileIntW(const WCHAR*, const WCHAR*, i32, const WCHAR*);
IMP BOOL    WINAPI DisableThreadLibraryCalls(HMODULE);
IMP void    WINAPI Sleep(u32);
IMP void    WINAPI OutputDebugStringA(const char*);

static HMODULE g_self, g_real;
static i32 g_fpsLimit = 0;   /* 0 = off (vanilla) */
static i32 g_vsync = -1;     /* -1 = game decides (vanilla), 0 = force off, 1 = force on */
static i64 g_freq, g_period, g_next;
static HANDLE g_timer;

/* 64-bit unsigned divide without CRT helpers */
static u64 udiv64(u64 n, u64 d) {
    u64 q = 0, r = 0; int i;
    for (i = 63; i >= 0; --i) { r = (r << 1) | ((n >> i) & 1); if (r >= d) { r -= d; q |= (u64)1 << i; } }
    return q;
}

static void wcopy(WCHAR *dst, const WCHAR *src) { while ((*dst++ = *src++)) ; }
static void wcat(WCHAR *dst, const WCHAR *src) { while (*dst) dst++; wcopy(dst, src); }

static void load_config(void) {
    WCHAR path[600]; u32 n = GetModuleFileNameW(g_self, path, 560), i, slash = 0;
    for (i = 0; i < n; i++) if (path[i] == '\\' || path[i] == '/') slash = i + 1;
    path[slash] = 0; wcat(path, L"hexfix.ini");
    g_fpsLimit = (i32)GetPrivateProfileIntW(L"Display", L"FrameRateLimit", 0, path);
    g_vsync    = (i32)GetPrivateProfileIntW(L"Display", L"VSync", -1, path);
    if (g_fpsLimit < 0) g_fpsLimit = 0;
    if (g_fpsLimit > 0) {
        QueryPerformanceFrequency(&g_freq);
        g_period = (i64)udiv64((u64)g_freq, (u64)g_fpsLimit);
        g_timer = CreateWaitableTimerExW(0, 0, 0x2 /*HIGH_RESOLUTION*/, 0x1F0003);
    }
}

static HMODULE real(void) {
    if (!g_real) {
        WCHAR p[600]; u32 n = GetSystemDirectoryW(p, 560); p[n] = 0;
        wcat(p, L"\\d3d9.dll");
        g_real = LoadLibraryW(p);
    }
    return g_real;
}

/* ---- frame limiter ---- */
static void limit_frame(void) {
    i64 now; QueryPerformanceCounter(&now);
    if (g_next == 0 || now - g_next > g_period * 4) g_next = now;   /* first frame or long stall: resync */
    g_next += g_period;
    if (now < g_next) {
        i64 remain = g_next - now;
        /* sleep until ~1ms before the deadline, then spin */
        i64 us = (i64)udiv64((u64)remain * 1000000ull, (u64)g_freq);
        if (us > 1500) {
            if (g_timer) { i64 due = -(us - 1000) * 10; SetWaitableTimer(g_timer, &due, 0, 0, 0, 0); WaitForSingleObject(g_timer, 0xFFFFFFFF); }
            else Sleep((u32)udiv64((u64)(us - 1000), 1000));
        }
        do QueryPerformanceCounter(&now); while (now < g_next);
    }
}

/* ---- vtable hooks ---- */
typedef struct { u32 w, h, fmt, count, ms, msq, swap; HANDLE wnd; BOOL windowed, autods; u32 dsfmt, flags, refresh, interval; } PP;
#define INTERVAL_IMMEDIATE 0x80000000u

typedef HRESULT (WINAPI *CreateDevice_t)(void*, u32, u32, HANDLE, u32, PP*, void**);
typedef HRESULT (WINAPI *Reset_t)(void*, PP*);
typedef HRESULT (WINAPI *Present_t)(void*, const void*, const void*, HANDLE, const void*);
static CreateDevice_t o_CreateDevice; static Reset_t o_Reset; static Present_t o_Present;

static void apply_vsync(PP *pp) {
    if (!pp || g_vsync < 0) return;
    pp->interval = g_vsync ? 1u : INTERVAL_IMMEDIATE;
}

static void *hook(void *obj, int slot, void *fn) {
    void **vt = *(void***)obj; u32 old, tmp; void *orig = vt[slot];
    if (orig == fn) return 0;
    VirtualProtect(&vt[slot], 4, 0x40, &old); vt[slot] = fn; VirtualProtect(&vt[slot], 4, old, &tmp);
    return orig;
}

static HRESULT WINAPI my_Present(void *dev, const void *a, const void *b, HANDLE w, const void *c) {
    if (g_fpsLimit > 0) limit_frame();
    return o_Present(dev, a, b, w, c);
}
static HRESULT WINAPI my_Reset(void *dev, PP *pp) { apply_vsync(pp); return o_Reset(dev, pp); }
static HRESULT WINAPI my_CreateDevice(void *d3d, u32 ad, u32 type, HANDLE wnd, u32 fl, PP *pp, void **out) {
    HRESULT hr; apply_vsync(pp);
    hr = o_CreateDevice(d3d, ad, type, wnd, fl, pp, out);
    if (hr >= 0 && out && *out) {
        void *p;
        if ((p = hook(*out, 16, (void*)my_Reset)))   o_Reset = (Reset_t)p;
        if ((p = hook(*out, 17, (void*)my_Present))) o_Present = (Present_t)p;
    }
    return hr;
}

/* ---- exports ---- */
void* WINAPI Direct3DCreate9(u32 sdk) {
    typedef void* (WINAPI *fn_t)(u32); fn_t f; void *d3d, *p;
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, "Direct3DCreate9"))) return 0;
    d3d = f(sdk);
    if (d3d && (p = hook(d3d, 16, (void*)my_CreateDevice))) o_CreateDevice = (CreateDevice_t)p;
    return d3d;
}
HRESULT WINAPI Direct3DCreate9Ex(u32 sdk, void **out) {
    typedef HRESULT (WINAPI *fn_t)(u32, void**); fn_t f;
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, "Direct3DCreate9Ex"))) return (HRESULT)0x80004001;
    return f(sdk, out);   /* not used by the game; passthrough without hooks */
}
#define PERF(name, ret, defret, params, args) \
  ret WINAPI name params { typedef ret (WINAPI *fn_t) params; fn_t f; \
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, #name))) return defret; return f args; }
PERF(D3DPERF_BeginEvent, i32, -1, (u32 col, const WCHAR *n), (col, n))
PERF(D3DPERF_EndEvent, i32, -1, (void), ())
PERF(D3DPERF_GetStatus, u32, 0, (void), ())
PERF(D3DPERF_QueryRepeatFrame, BOOL, 0, (void), ())
void WINAPI D3DPERF_SetMarker(u32 col, const WCHAR *n) { typedef void (WINAPI *fn_t)(u32, const WCHAR*); fn_t f; if (real() && (f = (fn_t)GetProcAddress(g_real, "D3DPERF_SetMarker"))) f(col, n); }
void WINAPI D3DPERF_SetRegion(u32 col, const WCHAR *n) { typedef void (WINAPI *fn_t)(u32, const WCHAR*); fn_t f; if (real() && (f = (fn_t)GetProcAddress(g_real, "D3DPERF_SetRegion"))) f(col, n); }
void WINAPI D3DPERF_SetOptions(u32 o) { typedef void (WINAPI *fn_t)(u32); fn_t f; if (real() && (f = (fn_t)GetProcAddress(g_real, "D3DPERF_SetOptions"))) f(o); }

BOOL WINAPI DllMain(HMODULE h, u32 reason, PVOID r) {
    (void)r;
    if (reason == 1) { g_self = h; DisableThreadLibraryCalls(h); load_config(); }
    return 1;
}
