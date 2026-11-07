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
IMP HANDLE  WINAPI CreateFileW(const WCHAR*, u32, u32, PVOID, u32, u32, HANDLE);
IMP BOOL    WINAPI WriteFile(HANDLE, const void*, u32, u32*, PVOID);

int _fltused = 0x9875;  /* no CRT: satisfy float-usage marker */
static HMODULE g_self, g_real;
static i32 g_fpsLimit = 0;   /* 0 = off (vanilla) */
static i32 g_vsync = -1;     /* -1 = game decides (vanilla), 0 = force off, 1 = force on */
static i32 g_showfps = 0;    /* 1 = draw an FPS counter in the top-left corner */
static i32 g_wsfov = 1;      /* 1 = Hor+ FOV beyond 16:9 (fixes >180 deg FOV on ultrawide), 0 = vanilla */
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


/* ---- tiny log (hexfix.log next to the DLL), enabled by [Debug] Log=1 ---- */
static HANDLE g_log = (HANDLE)-1;
static void log_open(const WCHAR *dir) {
    WCHAR p[600]; wcopy(p, dir); wcat(p, L"hexfix.log");
    g_log = CreateFileW(p, 0x40000000 /*GENERIC_WRITE*/, 1 /*SHARE_READ*/, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
}
static void logs(const char *s) { u32 n = 0, w; if (g_log == (HANDLE)-1) return; while (s[n]) n++; WriteFile(g_log, s, n, &w, 0); }
static void logx(const char *label, u32 v) {
    char b[16]; int i; static const char hx[] = "0123456789ABCDEF";
    logs(label); b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; i++) b[2 + i] = hx[(v >> (28 - 4 * i)) & 15];
    b[10] = '\r'; b[11] = '\n'; b[12] = 0; logs(" "); logs(b);
}
static void logd(const char *label, u32 v) {
    char b[16]; int n = 15; b[n] = 0; b[--n] = '\n'; b[--n] = '\r';
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v && n > 0);
    logs(label); logs(" "); logs(b + n);
}

static void load_config(void) {
    WCHAR path[600]; u32 n = GetModuleFileNameW(g_self, path, 560), i, slash = 0;
    for (i = 0; i < n; i++) if (path[i] == '\\' || path[i] == '/') slash = i + 1;
    path[slash] = 0;
    if (GetPrivateProfileIntW(L"Debug", L"Log", 0, (wcat(path, L"hexfix.ini"), path))) { path[slash] = 0; log_open(path); wcat(path, L"hexfix.ini"); }
    logs("hexfix loaded\r\n");
    g_fpsLimit = (i32)GetPrivateProfileIntW(L"Display", L"FrameRateLimit", 0, path);
    g_vsync    = (i32)GetPrivateProfileIntW(L"Display", L"VSync", -1, path);
    g_wsfov    = (i32)GetPrivateProfileIntW(L"Display", L"WidescreenFOV", 1, path);
    g_showfps  = (i32)GetPrivateProfileIntW(L"Debug", L"ShowFPS", 0, path);
    QueryPerformanceFrequency(&g_freq);
    if (g_fpsLimit < 0) g_fpsLimit = 0;
    logd("FrameRateLimit", (u32)g_fpsLimit); logx("VSync", (u32)g_vsync); logd("WidescreenFOV", (u32)g_wsfov); logd("ShowFPS", (u32)g_showfps);
    if (g_fpsLimit > 0) {
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


/* ---- FPS overlay: 7-segment digits drawn with IDirect3DDevice9::ColorFill on the back buffer ---- */
typedef struct { i32 l, t, r, b; } RECT;
typedef HRESULT (WINAPI *GetBackBuffer_t)(void*, u32, u32, u32, void**);
typedef HRESULT (WINAPI *ColorFill_t)(void*, void*, const RECT*, u32);
typedef u32 (WINAPI *Release_t)(void*);
typedef HRESULT (WINAPI *SwapGetBB_t)(void*, u32, u32, void**);
typedef struct { u32 fmt, type, usage, pool, ms, msq, w, h; } SURFDESC;
typedef HRESULT (WINAPI *GetDesc_t)(void*, SURFDESC*);
static i64 g_fpsT0; static u32 g_fpsFrames, g_fpsShown;

/* segments: a=top b=top-right c=bottom-right d=bottom e=bottom-left f=top-left g=middle */
static const unsigned char kSeg[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
static void fill(void *dev, void *surf, i32 l, i32 t, i32 r, i32 b, u32 col) {
    RECT rc; rc.l = l; rc.t = t; rc.r = r; rc.b = b;
    { HRESULT hr = ((ColorFill_t)(*(void***)dev)[35])(dev, surf, &rc, col);
      static int logged; if (!logged) { logged = 1; logx("ColorFill hr", (u32)hr); } }
}
static void draw_digit(void *dev, void *s, i32 x, i32 y, i32 u, int d, u32 col) {
    /* digit cell is 4u wide, 7u tall; segment thickness u */
    unsigned m = kSeg[d];
    if (m & 0x01) fill(dev, s, x,       y,       x + 4*u, y + u,   col);
    if (m & 0x02) fill(dev, s, x + 3*u, y,       x + 4*u, y + 4*u, col);
    if (m & 0x04) fill(dev, s, x + 3*u, y + 3*u, x + 4*u, y + 7*u, col);
    if (m & 0x08) fill(dev, s, x,       y + 6*u, x + 4*u, y + 7*u, col);
    if (m & 0x10) fill(dev, s, x,       y + 3*u, x + u,   y + 7*u, col);
    if (m & 0x20) fill(dev, s, x,       y,       x + u,   y + 4*u, col);
    if (m & 0x40) fill(dev, s, x,       y + 3*u, x + 4*u, y + 4*u, col);
}
static void draw_fps(void *dev, void *swap) {
    void *bb = 0; SURFDESC sd; i64 now; u32 v, digits[6], n = 0, i; i32 u, x, y, w;
    QueryPerformanceCounter(&now);
    if (!g_fpsT0) g_fpsT0 = now;
    g_fpsFrames++;
    if (now - g_fpsT0 >= g_freq / 2) {                       /* update twice a second */
        g_fpsShown = (u32)udiv64((u64)g_fpsFrames * (u64)g_freq + (u64)(now - g_fpsT0) / 2, (u64)(now - g_fpsT0));
        g_fpsFrames = 0; g_fpsT0 = now;
    }
    { HRESULT hr = swap ? ((SwapGetBB_t)(*(void***)swap)[5])(swap, 0, 0, &bb)
                        : ((GetBackBuffer_t)(*(void***)dev)[18])(dev, 0, 0, 0, &bb);
      static int logged; if (logged < 3) { logged++; logx("GetBackBuffer hr", (u32)hr); }
      if (hr < 0 || !bb) return; }
    sd.h = 0;
    ((GetDesc_t)(*(void***)bb)[12])(bb, &sd);                /* IDirect3DSurface9::GetDesc */
    u = (i32)(sd.h ? sd.h : 600) / 120; if (u < 2) u = 2;
    v = g_fpsShown; do { digits[n++] = v % 10; v /= 10; } while (v && n < 6);
    x = u * 2; y = u * 2; w = (i32)n * 5 * u + u;
    fill(dev, bb, x - u, y - u, x + w, y + 8*u, 0xC0000000u);      /* dark backing box */
    for (i = 0; i < n; i++)
        draw_digit(dev, bb, x + (i32)i * 5 * u, y, u, (int)digits[n - 1 - i], 0xFF40FF40u);
    ((Release_t)(*(void***)bb)[2])(bb);
}

static int g_inPresent;
static HRESULT WINAPI my_Present(void *dev, const void *a, const void *b, HANDLE w, const void *c) {
    HRESULT hr;
    if (g_inPresent) return o_Present(dev, a, b, w, c);      /* already handled by game_present */
    { static u32 n; if (n < 3 || n == 100) logd("Device Present #", n); n++; }
    if (g_showfps) draw_fps(dev, 0);
    if (g_fpsLimit > 0) limit_frame();
    g_inPresent = 1; hr = o_Present(dev, a, b, w, c); g_inPresent = 0;
    return hr;
}
/* IDirect3DSwapChain9::Present (vtable slot 3) - used for additional swap chains */
typedef HRESULT (WINAPI *SwapPresent_t)(void*, const void*, const void*, HANDLE, const void*, u32);
typedef HRESULT (WINAPI *SwapGetDevice_t)(void*, void**);
static SwapPresent_t o_SwapPresent;
static HRESULT WINAPI my_SwapPresent(void *sc, const void *a, const void *b, HANDLE w, const void *c, u32 fl) {
    if (!g_inPresent) {                     /* not re-entered from device Present */
        { static u32 n; if (n < 3 || n == 100) logd("SwapChain Present #", n); n++; }
        if (g_showfps) { void *dev = 0; if (((SwapGetDevice_t)(*(void***)sc)[8])(sc, &dev) >= 0 && dev) { draw_fps(dev, sc); ((Release_t)(*(void***)dev)[2])(dev); } }
        if (g_fpsLimit > 0) limit_frame();
    }
    return o_SwapPresent(sc, a, b, w, c, fl);
}
static void *hook(void *obj, int slot, void *fn);
static void hook_swap(void *sc) {
    void *p; if (!sc) return;
    if ((p = hook(sc, 3, (void*)my_SwapPresent))) { o_SwapPresent = (SwapPresent_t)p; logs("hooked swapchain Present\r\n"); }
}

/* ---- game-side Present hook ----
 * plasma.exe has a single IDirect3DDevice9::Present call at 0x4F4285 (`call [ecx+0x44]; cmp eax,edi`).
 * The D3D9 runtime may swap a device's vtable at runtime, silently dropping vtable hooks, so we
 * redirect the game's call site instead. The stub calls game_present(), redoes `cmp eax,edi`, returns. */
static u32 g_presentRet;
static HRESULT WINAPI game_present(void *dev, const void *a, const void *b, HANDLE w, const void *c) {
    Present_t fn = (Present_t)(*(void***)dev)[17];
    HRESULT hr;
    if ((void*)fn == (void*)my_Present) fn = o_Present;
    { static u32 n; if (n < 3 || n == 1000) logd("Game Present #", n); n++; }
    if (g_showfps) draw_fps(dev, 0);
    if (g_fpsLimit > 0) limit_frame();
    g_inPresent = 2; hr = fn(dev, a, b, w, c); g_inPresent = 0;
    return hr;
}
static __declspec(naked) void stubPresent(void) { __asm {
    pop  dword ptr [g_presentRet]
    call game_present
    cmp  eax, edi
    push dword ptr [g_presentRet]
    ret } }
static void install_present_hook(void) {
    static const unsigned char expect[5] = {0xFF,0x51,0x44,0x3B,0xC7};
    unsigned char *p = (unsigned char*)0x4F4285; u32 old, tmp; int i;
    for (i = 0; i < 5; i++) if (p[i] != expect[i]) { logs("game Present hook: bytes differ, skipped\r\n"); return; }
    VirtualProtect(p, 5, 0x40, &old);
    p[0] = 0xE8; *(i32*)(p + 1) = (i32)((u32)stubPresent - (0x4F4285 + 5));
    VirtualProtect(p, 5, old, &tmp);
    logs("game Present hook installed\r\n");
}

typedef HRESULT (WINAPI *CreateASC_t)(void*, PP*, void**);
typedef HRESULT (WINAPI *GetSwapChain_t)(void*, u32, void**);
static CreateASC_t o_CreateASC; static GetSwapChain_t o_GetSwapChain;
static HRESULT WINAPI my_CreateASC(void *dev, PP *pp, void **out) {
    HRESULT hr = o_CreateASC(dev, pp, out); logx("CreateAdditionalSwapChain hr", (u32)hr);
    if (hr >= 0 && out) hook_swap(*out);
    return hr;
}
static HRESULT WINAPI my_GetSwapChain(void *dev, u32 i, void **out) {
    HRESULT hr = o_GetSwapChain(dev, i, out);
    if (hr >= 0 && out) hook_swap(*out);
    return hr;
}
static HRESULT WINAPI my_Reset(void *dev, PP *pp) { HRESULT hr; apply_vsync(pp); hr = o_Reset(dev, pp); logx("Reset hr", (u32)hr); return hr; }
static HRESULT WINAPI my_CreateDevice(void *d3d, u32 ad, u32 type, HANDLE wnd, u32 fl, PP *pp, void **out) {
    HRESULT hr; apply_vsync(pp);
    hr = o_CreateDevice(d3d, ad, type, wnd, fl, pp, out);
    logx("CreateDevice hr", (u32)hr); logx("  flags", fl); if (pp) { logd("  w", pp->w); logd("  h", pp->h); logd("  windowed", (u32)pp->windowed); logx("  interval", pp->interval); logd("  swapeffect", pp->swap); }
    if (hr >= 0 && out && *out) {
        void *p;
        if ((p = hook(*out, 16, (void*)my_Reset)))   o_Reset = (Reset_t)p;
        if ((p = hook(*out, 17, (void*)my_Present))) o_Present = (Present_t)p;
        if ((p = hook(*out, 13, (void*)my_CreateASC))) o_CreateASC = (CreateASC_t)p;
        if ((p = hook(*out, 14, (void*)my_GetSwapChain))) o_GetSwapChain = (GetSwapChain_t)p;
        if (o_GetSwapChain) { void *sc = 0; if (o_GetSwapChain(*out, 0, &sc) >= 0 && sc) { hook_swap(sc); ((Release_t)(*(void***)sc)[2])(sc); } }
        logx("  device vtbl", (u32)*(void**)*out); logx("  o_Present", (u32)o_Present);
    }
    return hr;
}


/* ---- widescreen FOV fix (patches plasma.exe at runtime) ----
 * Vanilla: fovW = FOVh * (width/height), minus 3.3 if > 80 deg (FOVh = 52.5).
 * Fine up to 16:9 (90 deg) but 32:9 yields ~183 deg -> broken projection.
 * Fix: keep vanilla up to 16:9; beyond that use Hor+ (constant vertical extent),
 * anchored to the vanilla 16:9 horizontal FOV so there is no jump at 16:9. */
static float x87_tan(float x) { float r; __asm { fld x
    fptan
    fstp st(0)
    fstp r } return r; }
static float x87_atan(float x) { float r; __asm { fld x
    fld1
    fpatan
    fstp r } return r; }
#define DEG2RAD 0.017453292519943295f
#define RAD2DEG 57.29577951308232f
static float vanilla_w(float h, float aspect) { float w = h * aspect; if (w > 80.f) w -= 3.3f; return w; }
static float fix_fovw(float w, float aspect) {
    float h = *(float*)0x9A09E8;              /* plVirtualCam1 FOVh */
    const float a169 = 16.f / 9.f;
    float t;
    if (!g_wsfov || aspect <= a169 + 0.01f) return w;
    t = x87_tan(vanilla_w(h, a169) * 0.5f * DEG2RAD) / a169;  /* effective vertical half-tangent */
    return 2.f * x87_atan(t * aspect) * RAD2DEG;
}
static void __cdecl fixA(float *pw, float aspect) { u32 o = (u32)(*pw * 10.f); *pw = fix_fovw(*pw, aspect); logd("FOV A in x10", o); logd("FOV A out x10", (u32)(*pw * 10.f)); }
static void __cdecl fixB(float *pw, i32 w, i32 h) { u32 o = (u32)(*pw * 10.f); if (h > 0) *pw = fix_fovw(*pw, (float)w / (float)h); logd("FOV B w", (u32)w); logd("FOV B h", (u32)h); logd("FOV B in x10", o); logd("FOV B out x10", (u32)(*pw * 10.f)); }

/* site A 0x4432D0 (startup): replaces `mov ecx,[0x9A09E8]`; fovW at [ebp-0x194], aspect float at [ebp-0x18C] */
static __declspec(naked) void stubA(void) { __asm {
    pushad
    pushfd
    lea  eax, [ebp-0x194]
    push dword ptr [ebp-0x18C]
    push eax
    call fixA
    add  esp, 8
    popfd
    popad
    mov  ecx, dword ptr ds:[0x9A09E8]
    ret } }
/* site B 0x4F726A (display reset): replaces `mov ecx,[0xA057F4]`; fovW at [ebp+0x14], width/height ints at [ebp+8]/[ebp+0xC] */
static __declspec(naked) void stubB(void) { __asm {
    pushad
    pushfd
    lea  eax, [ebp+0x14]
    push dword ptr [ebp+0xC]
    push dword ptr [ebp+0x8]
    push eax
    call fixB
    add  esp, 12
    popfd
    popad
    mov  ecx, dword ptr ds:[0xA057F4]
    ret } }

static int patch_call6(u32 at, const unsigned char *expect, void *target) {
    unsigned char *p = (unsigned char*)at; u32 old, tmp; int i;
    for (i = 0; i < 6; i++) if (p[i] != expect[i]) return 0;
    VirtualProtect(p, 6, 0x40, &old);
    p[0] = 0xE8; *(i32*)(p + 1) = (i32)((u32)target - (at + 5)); p[5] = 0x90;
    VirtualProtect(p, 6, old, &tmp);
    return 1;
}
static void install_fov_fix(void) {
    static const unsigned char a[6] = {0x8B,0x0D,0xE8,0x09,0x9A,0x00}, b[6] = {0x8B,0x0D,0xF4,0x57,0xA0,0x00};
    if (!g_wsfov) return;
    logd("FOV patch A", (u32)patch_call6(0x4432D0, a, (void*)stubA));   /* only patches the retail Hex Isle plasma.exe */
    logd("FOV patch B", (u32)patch_call6(0x4F726A, b, (void*)stubB));
}

/* ---- exports ---- */
void* WINAPI Direct3DCreate9(u32 sdk) {
    typedef void* (WINAPI *fn_t)(u32); fn_t f; void *d3d, *p;
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, "Direct3DCreate9"))) return 0;
    d3d = f(sdk);
    logx("Direct3DCreate9 sdk", sdk); logx("  d3d", (u32)d3d);
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
    if (reason == 1) { g_self = h; DisableThreadLibraryCalls(h); load_config(); install_fov_fix(); install_present_hook(); }
    return 1;
}
