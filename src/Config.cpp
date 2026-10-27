#include "Config.h"
#include "Util.h"
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define PRESET_DIR FX_DIR "/presets"

namespace {

const FxPreset kBuiltIn[] = {
    { "Natural",            "1_Natural.ini",         { 0.45f, 0.25f, 0.15f, 1.03f, 0.00f, 1.00f, 1.00f, 0.15f, {1.00f, 1.00f, 1.00f}, 0.12f, 0.00f, 0.00f, 1.00f } },
    { "Vivid",              "2_Vivid.ini",           { 0.50f, 0.35f, 0.20f, 1.06f, 0.00f, 1.00f, 1.12f, 0.35f, {1.00f, 1.00f, 1.00f}, 0.10f, 0.00f, 0.01f, 1.00f } },
    { "Cinematic",          "3_Cinematic.ini",       { 0.30f, 0.20f, 0.35f, 1.04f, 0.00f, 1.02f, 0.92f, 0.10f, {1.03f, 1.00f, 0.97f}, 0.30f, 0.20f, 0.02f, 1.00f } },
    { "Crisp",              "4_Crisp.ini",           { 0.90f, 0.45f, 0.10f, 1.02f, 0.00f, 1.00f, 1.00f, 0.10f, {1.00f, 1.00f, 1.00f}, 0.00f, 0.00f, 0.00f, 1.00f } },
    { "Warm",               "5_Warm.ini",            { 0.40f, 0.20f, 0.15f, 1.03f, 0.01f, 1.00f, 1.02f, 0.15f, {1.05f, 1.00f, 0.92f}, 0.15f, 0.00f, 0.00f, 1.00f } },
    { "Cool",               "6_Cool.ini",            { 0.40f, 0.20f, 0.15f, 1.03f, 0.00f, 1.00f, 1.00f, 0.15f, {0.94f, 1.00f, 1.06f}, 0.15f, 0.00f, 0.00f, 1.00f } },
    { "Noir",               "7_Noir.ini",            { 0.50f, 0.40f, 0.40f, 1.15f, 0.00f, 1.00f, 0.00f, 0.00f, {1.00f, 1.00f, 1.00f}, 0.35f, 0.35f, 0.03f, 0.97f } },
    { "Retro",              "8_Retro.ini",           { 0.25f, 0.10f, 0.25f, 0.98f, 0.01f, 1.05f, 0.80f, 0.00f, {1.06f, 1.02f, 0.90f}, 0.25f, 0.30f, 0.00f, 1.00f } },

    { "GTA Realism",        "09_GTA_Realism.ini",    { 0.40f, 0.30f, 0.35f, 1.08f, -0.01f, 1.05f, 0.92f, 0.15f, {1.02f, 1.00f, 0.98f}, 0.18f, 0.04f, 0.03f, 1.00f } },
    { "GTA Photoreal",      "10_GTA_Photoreal.ini",  { 0.50f, 0.40f, 0.30f, 1.10f, 0.00f, 1.08f, 0.88f, 0.20f, {1.01f, 1.00f, 0.99f}, 0.20f, 0.04f, 0.04f, 0.98f } },
    { "GTA Real Sunny",     "11_GTA_RealSunny.ini",  { 0.40f, 0.30f, 0.30f, 1.07f, 0.00f, 1.03f, 0.96f, 0.25f, {1.04f, 1.00f, 0.95f}, 0.15f, 0.03f, 0.03f, 0.98f } },
    { "GTA Golden Hour",    "12_GTA_GoldenHour.ini", { 0.35f, 0.25f, 0.30f, 1.06f, 0.00f, 1.02f, 1.00f, 0.25f, {1.07f, 1.01f, 0.90f}, 0.25f, 0.03f, 0.02f, 1.00f } },
    { "GTA Overcast",       "13_GTA_Overcast.ini",   { 0.45f, 0.45f, 0.30f, 1.10f, 0.00f, 1.04f, 0.75f, 0.05f, {0.98f, 1.00f, 1.02f}, 0.30f, 0.10f, 0.03f, 1.00f } },
    { "GTA Night Neon",     "14_GTA_NightNeon.ini",  { 0.40f, 0.30f, 0.25f, 1.08f, 0.00f, 0.95f, 1.15f, 0.35f, {0.97f, 0.99f, 1.05f}, 0.20f, 0.03f, 0.03f, 1.00f } },
    { "GTA Gritty",         "15_GTA_Gritty.ini",     { 0.55f, 0.55f, 0.35f, 1.12f, -0.01f, 1.06f, 0.70f, 0.00f, {1.02f, 1.00f, 0.96f}, 0.35f, 0.15f, 0.04f, 0.97f } },
    { "GTA Movie",          "16_GTA_Movie.ini",      { 0.30f, 0.25f, 0.45f, 1.06f, 0.00f, 1.04f, 0.90f, 0.15f, {1.04f, 1.00f, 0.97f}, 0.30f, 0.12f, 0.03f, 0.98f } },
    { "Dehaze",             "17_Dehaze.ini",         { 0.35f, 0.35f, 0.20f, 1.05f, 0.00f, 1.00f, 1.02f, 0.20f, {1.00f, 1.00f, 1.00f}, 0.00f, 0.00f, 0.05f, 0.98f } },
    { "Clean HD",           "18_CleanHD.ini",        { 0.80f, 0.30f, 0.10f, 1.03f, 0.00f, 1.00f, 1.00f, 0.10f, {1.00f, 1.00f, 1.00f}, 0.00f, 0.00f, 0.02f, 1.00f } },
    { "Hyper Vivid",        "19_HyperVivid.ini",     { 0.50f, 0.40f, 0.20f, 1.08f, 0.00f, 1.00f, 1.25f, 0.50f, {1.00f, 1.00f, 1.00f}, 0.10f, 0.00f, 0.02f, 1.00f } },
    { "Faded Film",         "20_FadedFilm.ini",      { 0.25f, 0.15f, 0.10f, 0.92f, 0.03f, 1.00f, 0.70f, 0.00f, {1.05f, 1.01f, 0.93f}, 0.30f, 0.25f, 0.00f, 0.96f } },
    { "GTA Cinematic",      "21_GTA_Cinematic.ini",  { 0.35f, 0.30f, 0.40f, 1.07f, 0.00f, 1.04f, 0.90f, 0.15f, {1.03f, 1.00f, 0.97f}, 0.28f, 0.10f, 0.03f, 0.98f } },
    { "Arcade CRT",         "22_ArcadeCRT.ini",      { 0.30f, 0.10f, 0.20f, 1.08f, 0.02f, 1.00f, 1.20f, 0.30f, {1.00f, 1.00f, 1.00f}, 0.35f, 0.08f, 0.02f, 1.00f } },
    { "Dreamy",             "23_Dreamy.ini",         { 0.00f, 0.00f, 0.15f, 0.96f, 0.02f, 0.97f, 1.05f, 0.25f, {1.03f, 1.00f, 1.00f}, 0.20f, 0.00f, 0.00f, 1.00f } },
};
const int kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);
const int kMaxPresets = 48;


const struct { const char* key; size_t off; } kExtra[] = {
    { "exposure", offsetof(FxParams, exposure) }, { "shadows", offsetof(FxParams, shadows) },
    { "highlights", offsetof(FxParams, highlights) }, { "teal_orange", offsetof(FxParams, tealOrange) },
    { "tint_gm", offsetof(FxParams, tintGM) }, { "glow", offsetof(FxParams, glow) },
    { "chroma", offsetof(FxParams, chroma) }, { "letterbox", offsetof(FxParams, letterbox) },
    { "scanlines", offsetof(FxParams, scanlines) }, { "colorblind", offsetof(FxParams, cbMode) },
    { "deband", offsetof(FxParams, deband) },
};
const int kExtraCount = sizeof(kExtra) / sizeof(kExtra[0]);

void ApplyExtras(FxPreset& pr) {
    FxParams& p = pr.p;
    const char* n = pr.name;
    if      (!strcmp(n, "Cinematic"))       { p.tealOrange = 0.25f; p.glow = 0.15f; }
    else if (!strcmp(n, "Retro"))           { p.scanlines = 0.35f; p.chroma = 0.25f; }
    else if (!strcmp(n, "GTA Realism"))     { p.glow = 0.12f; p.highlights = 0.15f; p.deband = 1; }
    else if (!strcmp(n, "GTA Photoreal"))   { p.glow = 0.15f; p.highlights = 0.25f; p.shadows = -0.10f; p.deband = 1; }
    else if (!strcmp(n, "GTA Real Sunny"))  { p.glow = 0.18f; p.highlights = 0.20f; p.exposure = 0.05f; }
    else if (!strcmp(n, "GTA Golden Hour")) { p.glow = 0.30f; p.tealOrange = 0.15f; }
    else if (!strcmp(n, "GTA Overcast"))    { p.highlights = 0.20f; p.shadows = 0.05f; }
    else if (!strcmp(n, "GTA Night Neon"))  { p.glow = 0.40f; p.shadows = -0.05f; }
    else if (!strcmp(n, "GTA Gritty"))      { p.chroma = 0.10f; p.shadows = -0.10f; }
    else if (!strcmp(n, "GTA Movie"))       { p.tealOrange = 0.35f; p.glow = 0.20f; }
    else if (!strcmp(n, "GTA Cinematic"))   { p.tealOrange = 0.40f; p.glow = 0.25f; p.letterbox = 2.39f; p.chroma = 0.08f; }
    else if (!strcmp(n, "Clean HD"))        { p.deband = 1; }
    else if (!strcmp(n, "Faded Film"))      { p.shadows = 0.15f; p.chroma = 0.10f; }
    else if (!strcmp(n, "Arcade CRT"))      { p.scanlines = 0.60f; p.chroma = 0.35f; p.glow = 0.35f; }
    else if (!strcmp(n, "Dreamy"))          { p.glow = 0.60f; p.highlights = -0.10f; }
}

FxPreset Factory(int i) { FxPreset pr = kBuiltIn[i]; ApplyExtras(pr); return pr; }

FxPreset g_presets[kMaxPresets];
int  g_count = 0;
int  g_index = 0;
bool g_enabled = true;
FxUi g_ui = { false, 0.5f, false, false };

char* Trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

template <typename Fn> bool ReadIni(const char* path, Fn fn) {
    FILE* fp = fopen(path, "r");
    if (!fp) return false;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char* s = Trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        fn(Trim(s), Trim(eq + 1));
    }
    fclose(fp);
    return true;
}

void SetParam(FxPreset& pr, const char* k, const char* v) {
    FxParams& p = pr.p;
    const float f = (float)atof(v);
    if      (!strcmp(k, "name"))       snprintf(pr.name, sizeof(pr.name), "%s", v);
    else if (!strcmp(k, "sharpen"))    p.sharpen = f;
    else if (!strcmp(k, "clarity"))    p.clarity = f;
    else if (!strcmp(k, "curve"))      p.curve = f;
    else if (!strcmp(k, "contrast"))   p.contrast = f;
    else if (!strcmp(k, "brightness")) p.brightness = f;
    else if (!strcmp(k, "gamma"))      p.gamma = f;
    else if (!strcmp(k, "saturation")) p.saturation = f;
    else if (!strcmp(k, "vibrance"))   p.vibrance = f;
    else if (!strcmp(k, "tint_r"))     p.tint[0] = f;
    else if (!strcmp(k, "tint_g"))     p.tint[1] = f;
    else if (!strcmp(k, "tint_b"))     p.tint[2] = f;
    else if (!strcmp(k, "vignette"))   p.vignette = f;
    else if (!strcmp(k, "grain"))      p.grain = f;
    else if (!strcmp(k, "black"))      p.black = f;
    else if (!strcmp(k, "white"))      p.white = f;
    else for (int i = 0; i < kExtraCount; ++i) if (!strcmp(k, kExtra[i].key)) { *(float*)((char*)&p + kExtra[i].off) = f; return; }
}

bool WritePresetFile(const FxPreset& pr) {
    char path[128]; snprintf(path, sizeof(path), PRESET_DIR "/%s", pr.file);
    FILE* fp = fopen(path, "w");
    if (!fp) return false;
    const FxParams& p = pr.p;
    fprintf(fp, "# XeMoFX preset. Edit here or in the menu (L1+Square); every .ini in this folder is listed in the menu.\n"
                "name = %s\n\n"
                "# 0..1\nsharpen = %.3f\nclarity = %.3f\ncurve = %.3f\nvignette = %.3f\ngrain = %.3f\n\n"
                "# levels: black 0 / white 1 = unchanged\nblack = %.3f\nwhite = %.3f\n\n"
                "# 1 = unchanged\ncontrast = %.3f\ngamma = %.3f\nsaturation = %.3f\n\n"
                "# 0 = unchanged\nbrightness = %.3f\nvibrance = %.3f\n\n"
                "# colour multipliers, 1 = unchanged\ntint_r = %.3f\ntint_g = %.3f\ntint_b = %.3f\n\n"
                "# effect switches (0 = off)\n",
            pr.name, p.sharpen, p.clarity, p.curve, p.vignette, p.grain, p.black, p.white, p.contrast, p.gamma, p.saturation,
            p.brightness, p.vibrance, p.tint[0], p.tint[1], p.tint[2]);
    fprintf(fp, "\n# more effects (0 = off). letterbox = aspect e.g. 2.39, colorblind 1 protan / 2 deutan / 3 tritan\n");
    for (int i = 0; i < kExtraCount; ++i) fprintf(fp, "%s = %.3f\n", kExtra[i].key, *(const float*)((const char*)&p + kExtra[i].off));
    fclose(fp);
    return true;
}

bool ReadPresetFile(const char* file, FxPreset& pr) {
    char path[128]; snprintf(path, sizeof(path), PRESET_DIR "/%s", file);
    return ReadIni(path, [&](const char* k, const char* v) { SetParam(pr, k, v); });
}

void LoadPresetDir() {
    SceKernelStat st;
    if (sceKernelStat(PRESET_DIR, &st) != 0) {
        sceKernelMkdir(FX_DIR, 0777);
        sceKernelMkdir(PRESET_DIR, 0777);
    }
    for (int i = 0; i < kBuiltInCount; ++i) {
        char path[128]; snprintf(path, sizeof(path), PRESET_DIR "/%s", kBuiltIn[i].file);
        if (sceKernelStat(path, &st) != 0) WritePresetFile(Factory(i));
    }
    int fd = sceKernelOpen(PRESET_DIR, SCE_KERNEL_O_RDONLY | SCE_KERNEL_O_DIRECTORY, 0);
    if (fd < 0) return;
    static char names[kMaxPresets][64]; int nn = 0;
    char buf[4096]; int r;
    while ((r = sceKernelGetdents(fd, buf, sizeof(buf))) > 0) {
        for (int off = 0; off < r; ) {
            SceKernelDirent* d = (SceKernelDirent*)(buf + off);
            if (!d->d_reclen) break;
            off += d->d_reclen;
            const size_t len = strlen(d->d_name);
            if (len < 5 || len >= sizeof(names[0]) || strcmp(d->d_name + len - 4, ".ini") || nn >= kMaxPresets) continue;
            strcpy(names[nn++], d->d_name);
        }
    }
    sceKernelClose(fd);
    for (int i = 1; i < nn; ++i) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; --j) {
        char t[64]; strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
    }
    for (int i = 0; i < nn; ++i) {
        int slot = -1;
        for (int j = 0; j < g_count; ++j) if (!strcmp(g_presets[j].file, names[i])) slot = j;
        if (slot < 0) {
            if (g_count >= kMaxPresets) break;
            slot = g_count++;
            g_presets[slot] = kBuiltIn[0];
            snprintf(g_presets[slot].name, sizeof(g_presets[slot].name), "%.*s", (int)strlen(names[i]) - 4, names[i]);
            snprintf(g_presets[slot].file, sizeof(g_presets[slot].file), "%s", names[i]);
        }
        ReadPresetFile(names[i], g_presets[slot]);
    }
}

void ApplyState(const char* path) {
    ReadIni(path, [](const char* k, const char* v) {
        if (!strcmp(k, "enabled")) g_enabled = atoi(v) != 0;
        else if (!strcmp(k, "compare")) g_ui.compare = atoi(v) != 0;
        else if (!strcmp(k, "compare_pos")) { g_ui.comparePos = (float)atof(v); if (g_ui.comparePos < 0.1f || g_ui.comparePos > 0.9f) g_ui.comparePos = 0.5f; }
        else if (!strcmp(k, "fps")) g_ui.fps = atoi(v) != 0;
        else if (!strcmp(k, "menu_right")) g_ui.menuRight = atoi(v) != 0;
        else if (!strcmp(k, "preset")) {
            for (int i = 0; i < g_count; ++i) if (!strcasecmp(g_presets[i].name, v)) g_index = i;
        }
    });
}

}

namespace FxConfig {

void Load() {
    g_count = 0;
    for (int i = 0; i < kBuiltInCount; ++i) g_presets[g_count++] = Factory(i);
    LoadPresetDir();
    g_index = 0; g_enabled = true;
    ApplyState(FX_DIR "/XeMoFX.ini");
    char path[96]; snprintf(path, sizeof(path), FX_DIR "/%s.ini", FxTitleId());
    if (FxTitleId()[0]) ApplyState(path);
    FxLog("[CFG] %d presets, current '%s', enabled %d", g_count, g_presets[g_index].name, (int)g_enabled);
}

bool Enabled() { return g_enabled; }
void SetEnabled(bool on) { g_enabled = on; }
int  PresetCount() { return g_count; }
int  PresetIndex() { return g_index; }
const char* PresetName(int idx) { return (idx >= 0 && idx < g_count) ? g_presets[idx].name : ""; }
void SetPreset(int idx) { if (g_count) g_index = ((idx % g_count) + g_count) % g_count; }
const FxPreset& Current() { return g_presets[g_index]; }
FxParams& Edit() { return g_presets[g_index].p; }

bool SaveCurrent() {
    sceKernelMkdir(FX_DIR, 0777);
    sceKernelMkdir(PRESET_DIR, 0777);
    return WritePresetFile(g_presets[g_index]);
}

bool SaveAsNew(char* nameOut, int cap) {
    if (g_count >= kMaxPresets) return false;
    FxPreset pr = g_presets[g_index];
    for (int n = 1; n < 1000; ++n) {
        snprintf(pr.name, sizeof(pr.name), "Custom %d", n);
        bool used = false;
        for (int j = 0; j < g_count; ++j) if (!strcasecmp(g_presets[j].name, pr.name)) used = true;
        if (!used) break;
    }
    snprintf(pr.file, sizeof(pr.file), "%s.ini", pr.name);
    g_presets[g_count] = pr;
    g_index = g_count++;
    if (nameOut) snprintf(nameOut, cap, "%s", pr.name);
    return SaveCurrent();
}

void ResetCurrent() {
    FxPreset& pr = g_presets[g_index];
    for (int i = 0; i < kBuiltInCount; ++i) if (!strcmp(kBuiltIn[i].file, pr.file)) pr.p = Factory(i).p;
    ReadPresetFile(pr.file, pr);
}

bool DefaultCurrent() {
    FxPreset& pr = g_presets[g_index];
    for (int i = 0; i < kBuiltInCount; ++i) if (!strcmp(kBuiltIn[i].file, pr.file)) { pr.p = Factory(i).p; return true; }
    return false;
}

FxUi& Ui() { return g_ui; }

void Save() {
    if (!FxTitleId()[0]) return;
    char path[96]; snprintf(path, sizeof(path), FX_DIR "/%s.ini", FxTitleId());
    FILE* fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "# XeMoFX settings for %s (written by the plugin)\nenabled = %d\npreset = %s\ncompare = %d\ncompare_pos = %.2f\nfps = %d\nmenu_right = %d\n",
            FxTitleId(), (int)g_enabled, g_presets[g_index].name, (int)g_ui.compare, g_ui.comparePos, (int)g_ui.fps, (int)g_ui.menuRight);
    fclose(fp);
}

}
