#include "Util.h"
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void FxLog(const char* fmt, ...) {
#ifdef XEMOFX_LOG
    static bool s_reset = false;
    char line[512];
    va_list a; va_start(a, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, a);
    va_end(a);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    sceKernelMkdir(FX_DIR, 0777);
    int fd = sceKernelOpen(FX_DIR "/XeMoFX.log", SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | (s_reset ? SCE_KERNEL_O_APPEND : SCE_KERNEL_O_TRUNC), 0777);
    s_reset = true;
    if (fd >= 0) { sceKernelWrite(fd, line, (size_t)n); sceKernelClose(fd); }
#else
    (void)fmt;
#endif
}

struct NotifyBuffer {
    int  type, reqId, priority, msgId, targetId, userId, unk1, unk2, appId, errorNum, unk3;
    char useIconImageUri;
    char message[1024];
    char uri[1024];
    char unkstr[1024];
};

void FxNotify(const char* fmt, ...) {
    NotifyBuffer b;
    memset(&b, 0, sizeof(b));
    va_list a; va_start(a, fmt);
    vsnprintf(b.message, sizeof(b.message), fmt, a);
    va_end(a);
    b.useIconImageUri = 1;
    b.targetId = -1;
    strcpy(b.uri, "cxml://psnotification/tex_icon_system");
    int fd = sceKernelOpen("/dev/notification0", SCE_KERNEL_O_WRONLY, 0);
    if (fd < 0) fd = sceKernelOpen("/dev/notification1", SCE_KERNEL_O_WRONLY, 0);
    if (fd < 0) return;
    sceKernelWrite(fd, &b, sizeof(b));
    sceKernelClose(fd);
}

const char* FxTitleId() {
    static char tid[16] = "";
    static bool done = false;
    if (done) return tid;
    done = true;
    void* getAppInfo = nullptr; void* getPid = nullptr;
    if (sceKernelDlsym(0x2001, "sceKernelGetAppInfo", &getAppInfo) != 0 || !getAppInfo) return tid;
    if (sceKernelDlsym(0x2001, "getpid", &getPid) != 0 || !getPid) return tid;
    struct { uint32_t appId; uint64_t unk1; char titleId[14]; char unk2[0x3c]; } appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    if (((int (*)(int, void*))getAppInfo)(((int (*)())getPid)(), &appInfo) == 0) { memcpy(tid, appInfo.titleId, 9); tid[9] = 0; }
    return tid;
}

static int FindModule(const char* name) {
    SceKernelModule mods[256]; size_t n = 0;
    if (sceKernelGetModuleList(mods, 256, &n) < 0) return 0;
    for (size_t i = 0; i < n; ++i) {
        SceKernelModuleInfo info; memset(&info, 0, sizeof(info)); info.size = sizeof(info);
        if (sceKernelGetModuleInfo(mods[i], &info) >= 0 && strstr(info.name, name)) return mods[i];
    }
    return 0;
}

void* FxResolve(const char* mod, const char* sym) {
    int m = FindModule(mod);
    if (m <= 0) {
        char file[64]; snprintf(file, sizeof(file), "%s.sprx", mod);
        m = sceKernelLoadStartModule(file, 0, nullptr, 0, nullptr, nullptr);
        if (m <= 0) { static int s_n = 0; if (s_n++ < 8) FxLog("[HOOK] load %s -> 0x%x", file, m); }
    }
    void* p = nullptr;
    if (m <= 0 || sceKernelDlsym(m, sym, &p) != 0) return nullptr;
    return p;
}

static bool IsSystemModule(const char* n) {
    return !strncmp(n, "libSce", 6) || !strncmp(n, "libkernel", 9) || !strncmp(n, "libc", 4) || !strncmp(n, "libSceLibcInternal", 18);
}

int FxPatchImports(void* const* orig, void* const* hook, int n) {
    SceKernelModule mods[256]; size_t nm = 0;
    if (sceKernelGetModuleList(mods, 256, &nm) < 0) return 0;
    const uintptr_t self = (uintptr_t)&FxPatchImports;
    int patched = 0;
    for (size_t m = 0; m < nm; ++m) {
        SceKernelModuleInfo info; memset(&info, 0, sizeof(info)); info.size = sizeof(info);
        if (sceKernelGetModuleInfo(mods[m], &info) < 0 || IsSystemModule(info.name)) continue;
        bool isSelf = false;
        for (uint32_t s = 0; s < info.numSegments && s < SCE_KERNEL_MAX_SEGMENTS; ++s) {
            uintptr_t b = (uintptr_t)info.segmentInfo[s].baseAddr;
            if (self >= b && self < b + info.segmentInfo[s].size) isSelf = true;
        }
        if (isSelf) continue;
        for (uint32_t s = 0; s < info.numSegments && s < SCE_KERNEL_MAX_SEGMENTS; ++s) {
            if (info.segmentInfo[s].prot & SCE_KERNEL_PROT_CPU_EXEC) continue;
            uint64_t* w = (uint64_t*)info.segmentInfo[s].baseAddr;
            size_t words = info.segmentInfo[s].size / 8;
            for (size_t i = 0; i < words; ++i) {
                uint64_t v = w[i];
                if (v < 0x10000) continue;
                for (int f = 0; f < n; ++f) {
                    if (!orig[f] || v != (uint64_t)orig[f]) continue;
                    uint64_t page = (uint64_t)&w[i] & ~0x3FFFULL;
                    sceKernelMprotect((void*)page, 0x4000, SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_WRITE);
                    w[i] = (uint64_t)hook[f];
                    ++patched;
                    break;
                }
            }
        }
    }
    return patched;
}
