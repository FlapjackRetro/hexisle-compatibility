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
typedef struct { i32 x, y; } POINT;
IMP BOOL    WINAPI GetCursorPos(POINT*);
IMP BOOL    WINAPI ScreenToClient(HANDLE, POINT*);
IMP BOOL    WINAPI SetCursorPos(i32, i32);
typedef struct { u16 usUsagePage, usUsage; u32 dwFlags; HANDLE hwndTarget; } RAWINPUTDEVICE;
IMP BOOL    WINAPI RegisterRawInputDevices(const RAWINPUTDEVICE*, u32, u32);
IMP u32     WINAPI GetRawInputData(HANDLE, u32, void*, u32*, u32);
IMP i32     WINAPI SetWindowLongA(HANDLE, i32, i32);
typedef i32 (WINAPI *WNDPROC_t)(HANDLE, u32, u32, i32);
IMP i32     WINAPI CallWindowProcA(WNDPROC_t, HANDLE, u32, u32, i32);

int _fltused = 0x9875;  /* no CRT: satisfy float-usage marker */
void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
static HMODULE g_self, g_real;
static i32 g_fpsLimit = 0;   /* 0 = off (vanilla) */
static i32 g_vsync = -1;     /* -1 = game decides (vanilla), 0 = force off, 1 = force on */
static i32 g_showfps = 0;    /* 1 = draw an FPS counter in the top-left corner */
static i32 g_wsfov = 1;
static i32 g_modefix = 1;
static i32 g_mousefix = 1;
static i32 g_scaletext = 1, g_textref = 768;  /* resolution-independent GUI text boxes */  /* 1 = mouse deltas from the real cursor position (fixes camera jumps) */   /* 1 = keep wide fullscreen modes (e.g. 5120x1440) across restarts */      /* 1 = Hor+ FOV beyond 16:9 (fixes >180 deg FOV on ultrawide), 0 = vanilla */
static i64 g_freq, g_period, g_next;
static HANDLE g_timer;

/* 64-bit unsigned divide without CRT helpers */
static u64 udiv64(u64 n, u64 d) {
    /* restoring division on 32-bit halves: no CRT helpers (__aulldiv/__aullshr) with MSVC or clang */
    u32 nh = (u32)(n >> 32), nl = (u32)n, dh = (u32)(d >> 32), dl = (u32)d;
    u32 qh = 0, ql = 0, rh = 0, rl = 0; int i;
    for (i = 63; i >= 0; --i) {
        u32 bit = i >= 32 ? (nh >> (i - 32)) & 1 : (nl >> i) & 1;
        rh = (rh << 1) | (rl >> 31); rl = (rl << 1) | bit;           /* r = (r << 1) | bit */
        if (rh > dh || (rh == dh && rl >= dl)) {                     /* r >= d */
            u32 borrow = rl < dl; rl -= dl; rh -= dh + borrow;
            if (i >= 32) qh |= 1u << (i - 32); else ql |= 1u << i;
        }
    }
    return ((u64)qh << 32) | ql;
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
    g_modefix  = (i32)GetPrivateProfileIntW(L"Display", L"KeepWideModes", 1, path);
    g_mousefix = (i32)GetPrivateProfileIntW(L"Input", L"MouseFix", 1, path);
    g_scaletext = (i32)GetPrivateProfileIntW(L"Display", L"ScaleText", 1, path);
    g_textref  = (i32)GetPrivateProfileIntW(L"Display", L"TextReferenceHeight", 768, path);
    if (g_textref < 240) g_textref = 240; if (g_textref > 4320) g_textref = 4320;
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


/* ---- diagnostics: render target / texture creation (Log=1) ---- */
typedef HRESULT (WINAPI *CreateTexture_t)(void*, u32, u32, u32, u32, u32, u32, void**, HANDLE*);
typedef HRESULT (WINAPI *CreateRT_t)(void*, u32, u32, u32, u32, u32, BOOL, void**, HANDLE*);
static CreateTexture_t o_CreateTexture; static CreateRT_t o_CreateRT;
static HRESULT WINAPI my_CreateTexture(void *dev, u32 w, u32 h, u32 lv, u32 usage, u32 fmt, u32 pool, void **out, HANDLE *sh) {
    HRESULT hr = o_CreateTexture(dev, w, h, lv, usage, fmt, pool, out, sh);
    if ((usage & 1 /*RENDERTARGET*/) || hr < 0) { static int k; if (k < 80) { k++;
        logs(hr < 0 ? "CreateTexture FAILED" : "CreateTexture RT"); logd(" w", w); logd("  h", h); logx("  fmt", fmt); logx("  usage", usage); logx("  hr", (u32)hr);
        logx("  caller", (u32)_ReturnAddress()); } }
    return hr;
}
static HRESULT WINAPI my_CreateRT(void *dev, u32 w, u32 h, u32 fmt, u32 ms, u32 msq, BOOL lock, void **out, HANDLE *sh) {
    HRESULT hr = o_CreateRT(dev, w, h, fmt, ms, msq, lock, out, sh);
    { static int k; if (k < 40) { k++; logs("CreateRenderTarget"); logd(" w", w); logd("  h", h); logx("  fmt", fmt); logx("  hr", (u32)hr); logx("  caller", (u32)_ReturnAddress()); } }
    return hr;
}

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
    if (now - g_fpsT0 >= (g_freq >> 1)) {                       /* update twice a second */
        g_fpsShown = (u32)udiv64((u64)g_fpsFrames * (u64)g_freq + ((u64)(now - g_fpsT0) >> 1), (u64)(now - g_fpsT0));
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
        if (g_log != (HANDLE)-1) {
            if ((p = hook(*out, 23, (void*)my_CreateTexture))) o_CreateTexture = (CreateTexture_t)p;
            if ((p = hook(*out, 28, (void*)my_CreateRT))) o_CreateRT = (CreateRT_t)p;
        }
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
static void __cdecl fixA(float *pw, float aspect) { u32 o = (u32)(i32)(*pw * 10.f); *pw = fix_fovw(*pw, aspect); logd("FOV A in x10", o); logd("FOV A out x10", (u32)(i32)(*pw * 10.f)); }
static void __cdecl fixB(float *pw, i32 w, i32 h) { u32 o = (u32)(i32)(*pw * 10.f); if (h > 0) *pw = fix_fovw(*pw, (float)w / (float)h); logd("FOV B w", (u32)w); logd("FOV B h", (u32)h); logd("FOV B in x10", o); logd("FOV B out x10", (u32)(i32)(*pw * 10.f)); }

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


/* GUI dialogs (0x5EAEB0): dialog.fovX = dialog.fovY * guiMgr.aspect (aspect clamped to >= 4/3), linear in
 * degrees -> dialogs such as the in-level MenuBar badge get squeezed horizontally on ultrawide screens.
 * Same treatment as the main camera: vanilla up to 16:9, Hor+ anchored to the vanilla 16:9 result beyond. */
static void __cdecl fix_gui(unsigned char *rmod) {
    float fy = *(float*)(rmod + 0x48), a = *(float*)(*(unsigned char**)0xA05960 + 0x3C), w, t;
    const float a169 = 16.f / 9.f;
    w = fy * a;                                               /* vanilla */
    if (g_wsfov && a > a169 + 0.01f) {
        float w169 = fy * a169;
        t = (w169 < 170.f) ? x87_tan(w169 * 0.5f * DEG2RAD) / a169 : x87_tan(fy * 0.5f * DEG2RAD);
        w = 2.f * x87_atan(t * a) * RAD2DEG;
    }
    *(float*)(rmod + 0x44) = w;
    { static int k; if (k < 12) { k++; logd("GUI dialog fovY x10", (u32)(i32)(fy * 10.f)); logd("  aspect x1000", (u32)(i32)(a * 1000.f)); logd("  fovX x10", (u32)(i32)(w * 10.f)); } }
}
/* replaces `fld [eax+48h]; mov edx,[0A05960h]; fmul [edx+3Ch]; fstp [eax+44h]` at 0x5EAEB7 (15 bytes) */
static __declspec(naked) void stubGui(void) { __asm {
    pushad
    push eax
    call fix_gui
    add  esp, 4
    popad
    ret } }
static int patch_gui_site(u32 at) {
    static const unsigned char expect[15] = {0xD9,0x40,0x48,0x8B,0x15,0x60,0x59,0xA0,0x00,0xD8,0x4A,0x3C,0xD9,0x58,0x44};
    unsigned char *p = (unsigned char*)at; u32 old, tmp; int i;
    for (i = 0; i < 15; i++) if (p[i] != expect[i]) return 0;
    VirtualProtect(p, 15, 0x40, &old);
    p[0] = 0xE8; *(i32*)(p + 1) = (i32)((u32)stubGui - (at + 5));
    for (i = 5; i < 15; i++) p[i] = 0x90;
    VirtualProtect(p, 15, old, &tmp);
    return 1;
}
static void install_gui_fix(void) {
    if (!g_wsfov) return;
    logd("GUI FOV patch (dialog load)", (u32)patch_gui_site(0x5E9B31));
    logd("GUI FOV patch (aspect change)", (u32)patch_gui_site(0x5EAEB7));
}

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


/* ---- diagnostics: fullscreen mode validation at startup ----
 * 0x4412D5: plClient checks (Width,Height,Depth) from graphics.ini against the device record's mode list;
 * if absent it calls 0x440C90 (write default graphics.ini) at 0x441321. Log the list when that happens. */
typedef struct { u32 flags; u16 w, h; u32 depth; } MODEREC;   /* 0x20-byte entries, first 12 bytes */
static void __cdecl log_modecheck(u32 ebp) {
    u32 n = *(u16*)(ebp - 0x1E8);
    logs("Fullscreen mode not found in device list -> game resets graphics.ini to defaults\r\n");
    logd("  wanted w", *(u32*)0x991424); logd("  wanted h", *(u32*)0x991428); logd("  wanted depth", *(u32*)0x991430);
    logd("  device modes", n);

}
static __declspec(naked) void stubModeCheck(void) { __asm {
    pushad
    push ebp
    call log_modecheck
    add  esp, 4
    popad
    mov  eax, 0x440C90
    jmp  eax } }
typedef u32 (WINAPI *GetModeCount_t)(void*, u32, u32);
typedef HRESULT (WINAPI *EnumModes_t)(void*, u32, u32, u32, void*);
static GetModeCount_t o_GetModeCount; static EnumModes_t o_EnumModes;
static u32 WINAPI my_GetModeCount(void *d3d, u32 ad, u32 fmt) {
    u32 n = o_GetModeCount(d3d, ad, fmt);
    static int k; if (k < 12) { k++; logx("GetAdapterModeCount fmt", fmt); logd("  count", n); }
    return n;
}
static HRESULT WINAPI my_EnumModes(void *d3d, u32 ad, u32 fmt, u32 i, void *mode) {
    HRESULT hr = o_EnumModes(d3d, ad, fmt, i, mode);


    return hr;
}

/* ---- fullscreen mode persistence fix ----
 * At startup plClient (0x4412D5) looks for graphics.ini's (Width,Height,Depth) in the device record's mode
 * list. That list drops modes wider than 2560 (e.g. 3840x1080, 5120x1440) although D3D supports them, so the
 * game rewrites graphics.ini with 800x600 defaults. If the requested mode is missing but the adapter supports
 * it, retarget one record of the same depth (the largest; there are many duplicates) to the requested size. */
static int d3d_supports(u32 w, u32 h) {
    typedef void* (WINAPI *fn_t)(u32); fn_t f; void *d3d; u32 n, i; int ok = 0;
    u32 mode[4];
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, "Direct3DCreate9")) || !(d3d = f(32))) return 0;
    n = ((GetModeCount_t)(*(void***)d3d)[6])(d3d, 0, 22 /*X8R8G8B8*/);
    for (i = 0; i < n && !ok; i++)
        if (((EnumModes_t)(*(void***)d3d)[7])(d3d, 0, 22, i, mode) >= 0 && mode[0] == w && mode[1] == h) ok = 1;
    ((Release_t)(*(void***)d3d)[2])(d3d);
    return ok;
}
static void __cdecl fix_modelist(u32 ebp) {
    u32 n = *(u16*)(ebp - 0x1E8), i, w = *(u32*)0x991424, h = *(u32*)0x991428, depth = *(u32*)0x991430;
    unsigned char *arr = *(unsigned char**)(ebp - 0x1E4); MODEREC *best = 0;
    if (!g_modefix || !arr || !n) return;
    for (i = 0; i < n; i++) { MODEREC *m = (MODEREC*)(arr + i * 0x20); if (m->w == w && m->h == h && m->depth == depth) return; }
    if (w > 0xFFFF || h > 0xFFFF || !d3d_supports(w, h)) { logs("mode fix: requested mode not supported by D3D, leaving game default\r\n"); return; }
    for (i = 0; i < n; i++) { MODEREC *m = (MODEREC*)(arr + i * 0x20);
        if (m->depth == depth && (!best || (u32)m->w * m->h > (u32)best->w * best->h)) best = m; }
    if (!best) return;
    logd("mode fix: retargeting record w", best->w); logd("  h", best->h); logd("  to w", w); logd("  to h", h);
    best->w = (u16)w; best->h = (u16)h;
}
static __declspec(naked) void stubModeList(void) { __asm {
    pushad
    push ebp
    call fix_modelist
    add  esp, 4
    popad
    movzx edx, word ptr [ebp-0x1E8]
    ret } }
static void install_mode_fix(void) {
    static const unsigned char expect[7] = {0x0F,0xB7,0x95,0x18,0xFE,0xFF,0xFF};
    unsigned char *p = (unsigned char*)0x4412D5; u32 old, tmp; int i;
    if (!g_modefix) return;
    for (i = 0; i < 7; i++) if (p[i] != expect[i]) return;
    VirtualProtect(p, 7, 0x40, &old);
    p[0] = 0xE8; *(i32*)(p + 1) = (i32)((u32)stubModeList - (0x4412D5 + 5)); p[5] = 0x90; p[6] = 0x90;
    VirtualProtect(p, 7, old, &tmp);
    logs("fullscreen mode fix installed\r\n");
}


/* ---- mouse camera jump fix ----
 * WM_MOUSE* handler (0x4A6AA2) decodes lParam as unsigned (movzx / shr 16) and calls the mouse device's
 * OnMove(x,y) at 0x4A6950, which takes deltas against the last position and, when the cursor leaves the
 * central 0.3..0.7 band, warps the OS cursor to the window centre (0x4A6690, SetCursorPos).
 *  - with SetCapture active, positions left/above the window are negative -> read as ~65535 -> huge
 *    delta in the opposite direction;
 *  - messages queued before a warp are measured against the new centre -> false jumps.
 * Fix: feed OnMove the current cursor position (GetCursorPos + ScreenToClient: signed, post-warp). */

/* ---- raw mouse motion (used only to correct the first move after a cursor warp) ----
 * On some systems the game's warp to the window centre is undone by pending mouse motion: the cursor is
 * at the centre right after SetCursorPos but the next reading continues from the pre-warp position.
 * Raw input gives true relative motion regardless of where the cursor ends up. */
static WNDPROC_t g_origProc; static i32 g_rawDX, g_rawDY; static int g_rawOK, g_warpPending, g_rawAbsLogged;
static i32 WINAPI my_wndproc(HANDLE h, u32 msg, u32 wp, i32 lp) {
    if (msg == 0x00FF /*WM_INPUT*/) {
        unsigned char buf[64]; u32 sz = sizeof(buf);
        if (GetRawInputData((HANDLE)lp, 0x10000003 /*RID_INPUT*/, buf, &sz, 16) != (u32)-1 && *(u32*)buf == 0 /*RIM_TYPEMOUSE*/) {
            u16 flags = *(u16*)(buf + 16);
            if (!(flags & 1)) { g_rawDX += *(i32*)(buf + 28); g_rawDY += *(i32*)(buf + 32); }
            else if (!g_rawAbsLogged) { g_rawAbsLogged = 1; logs("raw input: mouse reports ABSOLUTE positions (remote/tablet/virtual device?)\r\n"); }
        }
    }
    return CallWindowProcA(g_origProc, h, msg, wp, lp);
}
static void raw_init(HANDLE hwnd) {
    RAWINPUTDEVICE rid; static int tried;
    if (tried || !hwnd) return; tried = 1;
    rid.usUsagePage = 1; rid.usUsage = 2; rid.dwFlags = 0; rid.hwndTarget = hwnd;   /* generic desktop / mouse, legacy messages kept */
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) { logs("raw input: register failed\r\n"); return; }
    g_origProc = (WNDPROC_t)SetWindowLongA(hwnd, -4 /*GWL_WNDPROC*/, (i32)my_wndproc);
    g_rawOK = g_origProc != 0;
    logd("raw input active", (u32)g_rawOK);
}

/* ---- mouse trace ring buffer (Log=1 only) ---- */
typedef struct { u32 t; i32 kind, a, b, c, d, e, f; } MEVT;   /* kind 1 = move (raw x,y, cursor x,y, last x,y), 2 = warp (target x,y, ok, after x,y) */
static MEVT g_ring[32]; static u32 g_ringPos;
static void ring_add(i32 kind, i32 a, i32 b, i32 c, i32 d, i32 e, i32 f) {
    MEVT *v = &g_ring[g_ringPos++ & 31]; i64 now; QueryPerformanceCounter(&now);
    v->t = (u32)udiv64((u64)now * 1000ull, (u64)(g_freq ? g_freq : 1)); v->kind = kind; v->a = a; v->b = b; v->c = c; v->d = d; v->e = e; v->f = f;
}
static void logi(const char *label, i32 v) { if (v < 0) { logs(label); logs(" -"); logd("", (u32)-v); } else logd(label, (u32)v); }
static void ring_dump(void) {
    u32 i; logs("---- mouse trace (oldest first) ----\r\n");
    for (i = 0; i < 32; i++) { MEVT *v = &g_ring[(g_ringPos + i) & 31]; if (!v->kind) continue;
        logd(v->kind == 1 ? "MOVE t_ms" : "WARP t_ms", v->t);
        if (v->kind == 1) { logi("  raw x", v->a); logi("  raw y", v->b); logi("  cur x", v->c); logi("  cur y", v->d); logi("  last x", v->e); logi("  last y", v->f); }
        else { logi("  target x", v->a); logi("  target y", v->b); logi("  ok", v->c); logi("  after x", v->d); logi("  after y", v->e); } }
    logs("------------------------------------\r\n");
}
/* replaces `call [SetCursorPos]` at 0x4A6726 (args already pushed: x, y in screen coords) */
/* The game sets the device's last position to the window centre *before* warping and assumes the warp
 * worked. On some systems SetCursorPos succeeds but the cursor doesn't move (or is moved straight back),
 * so the next delta is measured from a centre the cursor never reached -> camera jump.
 * Fix: after warping, store the cursor's actual position as the last position. */
static BOOL WINAPI my_SetCursorPos(i32 x, i32 y) {
    BOOL ok; POINT p; HANDLE hwnd = *(HANDLE*)0xA028AC;
    g_rawDX = g_rawDY = 0; g_warpPending = 1;
    ok = SetCursorPos(x, y);
    p.x = p.y = -99999;
    if (GetCursorPos(&p) && hwnd && ScreenToClient(hwnd, &p) && g_mousefix) {
        typedef struct { u32 vtbl; i32 lastX, lastY; } MDEV; MDEV *m = *(MDEV**)0xA028A8;
        if (m) { m->lastX = p.x; m->lastY = p.y; }
    }
    if (g_log != (HANDLE)-1) { POINT t; t.x = x; t.y = y; if (hwnd) ScreenToClient(hwnd, &t); ring_add(2, t.x, t.y, ok, p.x, p.y, 0); }
    return ok;
}
static void *g_pSetCursorPos = (void*)my_SetCursorPos;


typedef struct { u32 vtbl; i32 lastX, lastY, w, h; } MOUSEDEV;
static void __cdecl fix_mouse(i32 *xy) {
    MOUSEDEV *m = *(MOUSEDEV**)0xA028A8; HANDLE hwnd = *(HANDLE*)0xA028AC;
    i32 rx = xy[0], ry = xy[1], sx = (short)rx, sy = (short)ry, cx = rx, cy = ry;
    POINT p;
    if (g_mousefix && hwnd && GetCursorPos(&p) && ScreenToClient(hwnd, &p)) { cx = p.x; cy = p.y; xy[0] = cx; xy[1] = cy; }
    if (g_mousefix) {
        raw_init(hwnd);
        if (g_warpPending && m) {           /* first move after a warp: delta = true raw motion since the warp */
            g_warpPending = 0;
            if (g_rawOK) { m->lastX = cx - g_rawDX; m->lastY = cy - g_rawDY; }
        }
        g_rawDX = g_rawDY = 0;
    }
    if (g_log != (HANDLE)-1 && m && m->w > 0 && m->h > 0) {
        i32 dx = cx - m->lastX, dy = cy - m->lastY;           /* delta the game will now see */
        ring_add(1, sx, sy, cx, cy, m->lastX, m->lastY);
        if (dx > m->w / 8 || -dx > m->w / 8 || dy > m->h / 8 || -dy > m->h / 8) {
            static int k; if (k < 8) { k++; logs("LARGE mouse delta after fix\r\n"); logi("  dx", dx); logi("  dy", dy); ring_dump(); } }
    }
}
static __declspec(naked) void stubMouse(void) { __asm {
    push ecx
    lea  eax, [esp+8]
    push eax
    call fix_mouse
    add  esp, 4
    pop  ecx
    mov  eax, 0x4A6950
    jmp  eax } }
static void install_mouse_fix(void) {
    unsigned char *p = (unsigned char*)0x4A6AB5; u32 old, tmp;
    if (!g_mousefix && g_log == (HANDLE)-1) return;
    if (p[0] != 0xE8 || *(i32*)(p + 1) != (i32)(0x4A6950 - (0x4A6AB5 + 5))) { logs("mouse fix: bytes differ, skipped\r\n"); return; }
    VirtualProtect(p, 5, 0x40, &old);
    *(i32*)(p + 1) = (i32)((u32)stubMouse - (0x4A6AB5 + 5));
    VirtualProtect(p, 5, old, &tmp);
    logd("mouse hook installed, MouseFix", (u32)g_mousefix);
    {   /* warp fix/trace: retarget `call [0x8A17BC]` (SetCursorPos) at 0x4A6726 to our pointer */
        unsigned char *q = (unsigned char*)0x4A6726;
        if (q[0] == 0xFF && q[1] == 0x15 && *(u32*)(q + 2) == 0x8A17BC) {
            VirtualProtect(q, 6, 0x40, &old); *(u32*)(q + 2) = (u32)&g_pSetCursorPos; VirtualProtect(q, 6, old, &tmp);
            logs("warp hook installed\r\n"); }
    }
}


/* ---- diagnostics: GUI text box dynamic text maps (Log=1) ----
 * pfGUIControlMod::ISetUpDynTextMap (0x603B10): visW/H from control bounds * (pipe W/H, or 1024x768 if
 * control flag 4), extra = desired - visible, then plDynamicTextMap::Create (0x495230) and SetFont (0x494760). */
static void __cdecl log_dtm_create(u32 *args, u32 ctrl) {
    static int k; if (k >= 40) return; k++;
    logx("DynText Create ctrl", ctrl);
    logd("  visW", args[0] & 0xFFFF); logd("  visH", args[1] & 0xFFFF); logd("  alpha", args[2] & 0xFF);
    logd("  extraW", args[3] & 0xFFFF); logd("  extraH", args[4] & 0xFFFF);
}
static void __cdecl log_dtm_font(u32 *args) {
    static int k; if (k >= 40) return; k++;
    logd("  SetFont size", args[1] & 0xFFFF); logx("  flags", args[2]);
}
static __declspec(naked) void stubDtmCreate(void) { __asm {
    pushad
    push esi                /* control (this of ISetUpDynTextMap) */
    lea  eax, [esp+0x28]    /* args: pushad(32) + push(4) + ret(4) */
    push eax
    call log_dtm_create
    add  esp, 8
    popad
    mov  eax, 0x495230
    jmp  eax } }
static __declspec(naked) void stubDtmFont(void) { __asm {
    pushad
    lea  eax, [esp+0x24]
    push eax
    call log_dtm_font
    add  esp, 4
    popad
    mov  eax, 0x494760
    jmp  eax } }
static void retarget_call(u32 at, u32 expect_target, void *fn) {
    unsigned char *p = (unsigned char*)at; u32 old, tmp;
    if (p[0] != 0xE8 || *(i32*)(p + 1) != (i32)(expect_target - (at + 5))) return;
    VirtualProtect(p, 5, 0x40, &old); *(i32*)(p + 1) = (i32)((u32)fn - (at + 5)); VirtualProtect(p, 5, old, &tmp);
}

/* ---- resolution-independent GUI text (ScaleText) ----
 * pfGUIControlMod::ISetUpDynTextMap (0x603B10) sizes each text box's plDynamicTextMap as
 * (control bounds fraction) x (screen W,H) and draws a fixed-pixel font into it, so text shrinks as the
 * resolution grows (and narrows on wide screens, since the width fraction doesn't change with aspect).
 * Controls with flag 4 instead use a fixed 1024x768 virtual size (text scales with the control).
 * Force that path for all controls; the virtual size is TextReferenceHeight (default 768) at 4:3. */
static void install_text_fix(void) {
    unsigned char *j = (unsigned char*)0x603B51, *w = (unsigned char*)0x603B70, *h = (unsigned char*)0x603B75; u32 old, tmp;
    if (!g_scaletext) return;
    if (j[0] != 0x75 || j[1] != 0x1D || w[0] != 0xBF || *(u32*)(w + 1) != 0x400 || h[0] != 0xBB || *(u32*)(h + 1) != 0x300) { logs("text fix: bytes differ, skipped\r\n"); return; }
    VirtualProtect(j, 0x30, 0x40, &old);
    j[0] = 0xEB;                                              /* jne -> jmp: always use the virtual size */
    *(u32*)(h + 1) = (u32)g_textref;
    *(u32*)(w + 1) = (u32)((g_textref * 4 + 1) / 3);
    VirtualProtect(j, 0x30, old, &tmp);
    logd("text fix installed, reference height", (u32)g_textref);
}

static void install_text_diag(void) {
    if (g_log == (HANDLE)-1) return;
    retarget_call(0x603C13, 0x495230, (void*)stubDtmCreate);
    retarget_call(0x603C7E, 0x494760, (void*)stubDtmFont);
    logs("text diag installed\r\n");
}

static void install_diag(void) {
    unsigned char *p = (unsigned char*)0x441321; u32 old, tmp;
    if (g_log == (HANDLE)-1) return;
    if (p[0] != 0xE8 || *(i32*)(p + 1) != (i32)(0x440C90 - (0x441321 + 5))) return;
    VirtualProtect(p, 5, 0x40, &old);
    *(i32*)(p + 1) = (i32)((u32)stubModeCheck - (0x441321 + 5));
    VirtualProtect(p, 5, old, &tmp);
}

/* ---- exports ---- */
void* WINAPI Direct3DCreate9(u32 sdk) {
    typedef void* (WINAPI *fn_t)(u32); fn_t f; void *d3d, *p;
    if (!real() || !(f = (fn_t)GetProcAddress(g_real, "Direct3DCreate9"))) return 0;
    d3d = f(sdk);
    logx("Direct3DCreate9 sdk", sdk); logx("  d3d", (u32)d3d);
    if (d3d && (p = hook(d3d, 16, (void*)my_CreateDevice))) o_CreateDevice = (CreateDevice_t)p;
    if (d3d && g_log != (HANDLE)-1) {
        if ((p = hook(d3d, 6, (void*)my_GetModeCount))) o_GetModeCount = (GetModeCount_t)p;
        if ((p = hook(d3d, 7, (void*)my_EnumModes))) o_EnumModes = (EnumModes_t)p;
    }
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
    if (reason == 1) { g_self = h; DisableThreadLibraryCalls(h); load_config(); install_fov_fix(); install_gui_fix(); install_present_hook(); install_mode_fix(); install_mouse_fix(); install_text_fix(); install_text_diag(); install_diag(); }
    return 1;
}
