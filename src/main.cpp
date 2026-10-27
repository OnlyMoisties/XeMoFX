#include <kernel.h>
#include <pad.h>
#include <stdio.h>
#include "Util.h"
#include "Config.h"
#include "Fx.h"
#include "Menu.h"

extern "C" {
int sceGnmSubmitAndFlipCommandBuffers(uint32_t, void**, uint32_t*, void**, uint32_t*, uint32_t, uint32_t, uint32_t, int64_t);
int sceGnmSubmitCommandBuffers(uint32_t, void**, uint32_t*, void**, uint32_t*);
}

namespace {

void* FromOwnImport(void* stub) {
    const uint8_t* p = (const uint8_t*)stub;
    if (!p || p[0] != 0xFF || p[1] != 0x25) return nullptr;
    const uint64_t v = *(const uint64_t*)(p + 6 + *(const int32_t*)(p + 2));
    const uint64_t self = (uint64_t)&FromOwnImport;
    if (v < 0x10000 || (v > self - 0x1000000 && v < self + 0x1000000)) return nullptr;
    return (void*)v;
}

typedef int (*FlipFn)(uint32_t, void**, uint32_t*, void**, uint32_t*, uint32_t, uint32_t, uint32_t, int64_t);
typedef int (*FlipWLFn)(uint64_t, uint32_t, void**, uint32_t*, void**, uint32_t*, uint32_t, uint32_t, uint32_t, int64_t);
typedef int (*SubmitFn)(uint32_t, void**, uint32_t*, void**, uint32_t*);
typedef int (*SubmitWLFn)(uint64_t, uint32_t, void**, uint32_t*, void**, uint32_t*);
typedef int (*RegBufFn)(int, int, void* const*, int, const void*);
typedef int (*PadStateFn)(int, ScePadData*);
typedef int (*PadReadFn)(int, ScePadData*, int);

enum { H_FLIP, H_FLIPWL, H_SUBMIT, H_SUBMITWL, H_REGBUF, H_PADSTATE, H_PADREAD, H_PADREADEXT, H_COUNT };
void* g_orig[H_COUNT];

volatile bool g_toggleReq = false;
volatile int  g_presetStep = 0;

int g_activePad = -1;
uint64_t g_activeSeen = 0;

bool OnSample(int handle, const ScePadData& s) {
    const uint64_t now = sceKernelGetProcessTime();
    if (s.buttons) {
        if (g_activePad < 0 || (handle != g_activePad && now - g_activeSeen > 2000000)) g_activePad = handle;
        if (handle == g_activePad) g_activeSeen = now;
    }
    if (handle != g_activePad) return Menu::IsOpen();
    static uint32_t s_prev = 0;
    const uint32_t b = s.buttons, pressed = b & ~s_prev;
    s_prev = b;
    const bool open = Menu::OnPad(b);
    if (!open && (b & SCE_PAD_BUTTON_R3) && !(b & SCE_PAD_BUTTON_L3)) {
        if (pressed & SCE_PAD_BUTTON_LEFT)  g_toggleReq = true;
        if (pressed & SCE_PAD_BUTTON_RIGHT) __sync_fetch_and_add(&g_presetStep, 1);
        if (pressed & SCE_PAD_BUTTON_UP)    __sync_fetch_and_sub(&g_presetStep, 1);
    }
    return open;
}
void Blank(ScePadData& d) {
    d.buttons = 0;
    d.leftStick.x = d.leftStick.y = d.rightStick.x = d.rightStick.y = 128;
    d.analogButtons.l2 = d.analogButtons.r2 = 0;
}

int hkPadReadState(int h, ScePadData* d) {
    int r = ((PadStateFn)g_orig[H_PADSTATE])(h, d);
    if (r == 0 && d && OnSample(h, *d)) Blank(*d);
    return r;
}
int PadReadCommon(int h, ScePadData* d, int r) {
    if (r > 0 && d && OnSample(h, d[r - 1])) for (int i = 0; i < r; ++i) Blank(d[i]);
    return r;
}
int hkPadRead(int h, ScePadData* d, int n)    { return PadReadCommon(h, d, ((PadReadFn)g_orig[H_PADREAD])(h, d, n)); }
int hkPadReadExt(int h, ScePadData* d, int n) { return PadReadCommon(h, d, ((PadReadFn)g_orig[H_PADREADEXT])(h, d, n)); }

int hkRegBuf(int h, int start, void* const* addrs, int num, const void* attr) {
    int r = ((RegBufFn)g_orig[H_REGBUF])(h, start, addrs, num, attr);
    if (r >= 0 && addrs) Fx::RegisterBuffers(start, addrs, num, (const uint32_t*)attr);
    return r;
}

int hkSubmit(uint32_t c, void** d, uint32_t* ds, void** cc, uint32_t* cs) {
    Fx::ObserveSubmit(c, d, ds);
    return ((SubmitFn)g_orig[H_SUBMIT])(c, d, ds, cc, cs);
}
int hkSubmitWL(uint64_t wl, uint32_t c, void** d, uint32_t* ds, void** cc, uint32_t* cs) {
    Fx::ObserveSubmit(c, d, ds);
    return ((SubmitWLFn)g_orig[H_SUBMITWL])(wl, c, d, ds, cc, cs);
}

void PollSettings() {
    bool changed = false;
    if (g_toggleReq) {
        g_toggleReq = false;
        FxConfig::SetEnabled(!FxConfig::Enabled());
        char m[64]; snprintf(m, sizeof m, "xemofx %s%s", FxConfig::Enabled() ? "on - " : "off", FxConfig::Enabled() ? FxConfig::Current().name : "");
        Menu::Notify(m);
        changed = true;
    }
    const int step = __sync_lock_test_and_set(&g_presetStep, 0);
    if (step) {
        FxConfig::SetPreset(FxConfig::PresetIndex() + step);
        FxConfig::SetEnabled(true);
        char m[64]; snprintf(m, sizeof m, "%s  (%d/%d)", FxConfig::Current().name, FxConfig::PresetIndex() + 1, FxConfig::PresetCount());
        Menu::Notify(m);
        changed = true;
    }
    if (changed) FxConfig::Save();
}

int DoFlip(void* wlFn, uint64_t wl, uint32_t c, void** d, uint32_t* ds, void** cc, uint32_t* cs,
           uint32_t vo, uint32_t idx, uint32_t mode, int64_t arg) {
    PollSettings();
    {
        static uint64_t s_t0 = 0; static int s_n = 0;
        const uint64_t now = sceKernelGetProcessTime();
        if (!s_t0) s_t0 = now;
        ++s_n;
        if (now - s_t0 >= 500000) { Menu::SetFps(s_n * 1000000.0f / (float)(now - s_t0)); s_t0 = now; s_n = 0; }
    }
    uint32_t w = 0, h = 0; Fx::DisplaySize(w, h);
    ImDrawData* ui = (w && h) ? Menu::Frame(w, h) : nullptr;
    const FxParams* fx = FxConfig::Enabled() ? &FxConfig::Current().p : nullptr;
    if (fx || ui) {
        Fx::FlipArgs fa = { c, d, ds, cc, cs, vo, idx, mode, arg, g_orig[H_FLIP], wlFn, wl };
        if (Fx::Present(fa, fx, Menu::Split(), ui) == 0) return 0;
    }
    return wlFn ? ((FlipWLFn)wlFn)(wl, c, d, ds, cc, cs, vo, idx, mode, arg)
                : ((FlipFn)g_orig[H_FLIP])(c, d, ds, cc, cs, vo, idx, mode, arg);
}
int hkFlip(uint32_t c, void** d, uint32_t* ds, void** cc, uint32_t* cs, uint32_t vo, uint32_t idx, uint32_t mode, int64_t arg) {
    return DoFlip(nullptr, 0, c, d, ds, cc, cs, vo, idx, mode, arg);
}
int hkFlipWL(uint64_t wl, uint32_t c, void** d, uint32_t* ds, void** cc, uint32_t* cs, uint32_t vo, uint32_t idx, uint32_t mode, int64_t arg) {
    return DoFlip(g_orig[H_FLIPWL], wl, c, d, ds, cc, cs, vo, idx, mode, arg);
}

void* const g_hook[H_COUNT] = {
    (void*)hkFlip, (void*)hkFlipWL, (void*)hkSubmit, (void*)hkSubmitWL, (void*)hkRegBuf,
    (void*)hkPadReadState, (void*)hkPadRead, (void*)hkPadReadExt,
};

void* HookThread(void*) {
    static const char* const kGnm[] = { "libSceGnmDriver", "libSceGnmDriverForNeoMode", nullptr };
    static const char* const kVideo[] = { "libSceVideoOut", nullptr };
    static const char* const kPad[] = { "libScePad", nullptr };
    static const struct { const char* const* mods; const char* sym; } kSyms[H_COUNT] = {
        { kGnm,   "sceGnmSubmitAndFlipCommandBuffers" },
        { kGnm,   "sceGnmSubmitAndFlipCommandBuffersForWorkload" },
        { kGnm,   "sceGnmSubmitCommandBuffers" },
        { kGnm,   "sceGnmSubmitCommandBuffersForWorkload" },
        { kVideo, "sceVideoOutRegisterBuffers" },
        { kPad,   "scePadReadState" },
        { kPad,   "scePadRead" },
        { kPad,   "scePadReadExt" },
    };
    static const int kDelaysMs[] = { 0, 250, 500, 1000, 2000, 4000, 8000, 15000, 30000, 60000, 120000 };
    FxConfig::Load();
    int total = 0, elapsed = 0;
    for (unsigned s = 0; s < sizeof(kDelaysMs) / sizeof(kDelaysMs[0]); ++s) {
        sceKernelUsleep((kDelaysMs[s] - elapsed) * 1000);
        elapsed = kDelaysMs[s];
        for (int i = 0; i < H_COUNT; ++i)
            for (const char* const* m = kSyms[i].mods; *m && !g_orig[i]; ++m) g_orig[i] = FxResolve(*m, kSyms[i].sym);
        if (!g_orig[H_FLIP])   g_orig[H_FLIP]   = FromOwnImport((void*)&sceGnmSubmitAndFlipCommandBuffers);
        if (!g_orig[H_SUBMIT]) g_orig[H_SUBMIT] = FromOwnImport((void*)&sceGnmSubmitCommandBuffers);
        if (s == 0) FxLog("[HOOK] flip %p wl %p regbuf %p pad %p/%p", g_orig[H_FLIP], g_orig[H_FLIPWL], g_orig[H_REGBUF], g_orig[H_PADSTATE], g_orig[H_PADREAD]);
        const int n = FxPatchImports(g_orig, g_hook, H_COUNT);
        if (n) FxLog("[HOOK] +%d slot(s) at %d ms", n, elapsed);
        total += n;
        if (s == 0) FxNotify("XeMoFX loaded - %s%s\nL1+Square menu", FxConfig::Current().name, FxConfig::Enabled() ? "" : " (off)");
    }
    FxLog("[HOOK] scans done: %d slot(s) total", total);
    return nullptr;
}

}

#ifndef attr_public
#define attr_public
#endif

extern "C" int attr_public plugin_load(int, const char**) { return 0; }
extern "C" int attr_public plugin_unload(int, const char**) { return 0; }

extern "C" int module_start(size_t, const void*) {
    static bool started = false;
    if (started) return 0;
    started = true;
    FxLog("[XeMoFX] start in %s", FxTitleId());
    ScePthread t;
    if (scePthreadCreate(&t, nullptr, HookThread, nullptr, "xemofx_hook") == 0) scePthreadDetach(t);
    return 0;
}

extern "C" int module_stop(size_t, const void*) { return 0; }
