#include "Menu.h"
#include "Config.h"
#include "Util.h"
#include <kernel.h>
#include <pad.h>
#include <stdio.h>
#include <string.h>
#include "../third_party/imgui/imgui.h"
#include "../third_party/fonts/notosans_embedded.h"

namespace {

volatile uint32_t g_buttons = 0;
volatile bool g_open = false;
bool g_inited = false;
ImFont* g_font = nullptr;
char g_toast[64] = ""; float g_toastT = 0.0f;
float g_fps = 0.0f;
int g_page = 0, g_sel = 0, g_scroll = 0;
int g_mainSel = 0;

void Toast(const char* msg) { snprintf(g_toast, sizeof(g_toast), "%s", msg); g_toastT = 2.5f; }

void Init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    g_font = io.Fonts->AddFontFromMemoryTTF((void*)g_notoSansTtf, (int)g_notoSansTtfLen, 26.0f, &cfg);
    io.FontDefault = g_font;
    unsigned char* px = nullptr; int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    g_inited = true;
    FxLog("[MENU] ready, font atlas %dx%d", w, h);
}

enum Kind { K_TOGGLE, K_SLIDER, K_CHOICE, K_PAGE, K_ACTION, K_PRESET, K_INFO };
enum { P_MAIN, P_DETAIL, P_COLOUR, P_LIGHT, P_LENS, P_PRESETS, P_SETTINGS, P_COUNT };
enum Action { A_SAVE = 1, A_SAVENEW, A_UNDO, A_FACTORY };
const char* const kPageName[P_COUNT] = { "main", "detail", "colour", "light", "lens & film", "presets", "settings" };

struct Choice { const char* label; float value; };
const Choice kOnOff[]  = { { "off", 0.0f }, { "on", 1.0f } };
const Choice kCb[]     = { { "off", 0.0f }, { "protan", 1.0f }, { "deutan", 2.0f }, { "tritan", 3.0f } };
const Choice kBars[]   = { { "off", 0.0f }, { "2.39 : 1", 2.39f }, { "2.00 : 1", 2.0f }, { "1.85 : 1", 1.85f } };

struct Row {
    Kind kind; const char* label;
    float* v; float lo, hi, neutral;
    bool* flag;
    const Choice* ch; int nch; float* cv;
    int target;
};

float g_warmth = 0.0f;
bool  g_fxOn = true;
float g_menuSide = 0.0f;

void WarmthFromTint() { const FxParams& p = FxConfig::Current().p; g_warmth = (p.tint[0] - p.tint[2]) / 0.16f; if (g_warmth > 1) g_warmth = 1; if (g_warmth < -1) g_warmth = -1; }
void TintFromWarmth() { FxParams& p = FxConfig::Edit(); p.tint[0] = 1.0f + 0.08f * g_warmth; p.tint[2] = 1.0f - 0.08f * g_warmth; }

int BuildRows(int page, Row* r) {
    FxParams& p = FxConfig::Edit();
    FxUi& ui = FxConfig::Ui();
    int n = 0;
    auto S = [&](const char* l, float* v, float lo, float hi, float neu) { Row x = {}; x.kind = K_SLIDER; x.label = l; x.v = v; x.lo = lo; x.hi = hi; x.neutral = neu; r[n++] = x; };
    auto T = [&](const char* l, bool* f) { Row x = {}; x.kind = K_TOGGLE; x.label = l; x.flag = f; r[n++] = x; };
    auto C = [&](const char* l, const Choice* c, int nc, float* v) { Row x = {}; x.kind = K_CHOICE; x.label = l; x.ch = c; x.nch = nc; x.cv = v; r[n++] = x; };
    auto G = [&](const char* l, int pg) { Row x = {}; x.kind = K_PAGE; x.label = l; x.target = pg; r[n++] = x; };
    auto A = [&](const char* l, int a) { Row x = {}; x.kind = K_ACTION; x.label = l; x.target = a; r[n++] = x; };
    auto I = [&](const char* l) { Row x = {}; x.kind = K_INFO; x.label = l; r[n++] = x; };
    switch (page) {
    case P_MAIN:
        T("effects", &g_fxOn);
        { Row x = {}; x.kind = K_PRESET; x.label = "preset"; r[n++] = x; }
        T("compare", &ui.compare);
        G("detail", P_DETAIL); G("colour", P_COLOUR); G("light", P_LIGHT); G("lens & film", P_LENS);
        G("presets", P_PRESETS); G("settings", P_SETTINGS);
        break;
    case P_DETAIL:
        S("sharpen", &p.sharpen, 0.0f, 1.0f, 0.0f);
        S("clarity", &p.clarity, 0.0f, 1.0f, 0.0f);
        S("glow", &p.glow, 0.0f, 1.0f, 0.0f);
        C("deband", kOnOff, 2, &p.deband);
        break;
    case P_COLOUR:
        S("saturation", &p.saturation, 0.0f, 2.0f, 1.0f);
        S("vibrance", &p.vibrance, -1.0f, 1.0f, 0.0f);
        S("warmth", &g_warmth, -1.0f, 1.0f, 0.0f);
        S("green / magenta", &p.tintGM, -1.0f, 1.0f, 0.0f);
        S("teal & orange", &p.tealOrange, -1.0f, 1.0f, 0.0f);
        C("colour blind fix", kCb, 4, &p.cbMode);
        break;
    case P_LIGHT:
        S("exposure", &p.exposure, -2.0f, 2.0f, 0.0f);
        S("black level", &p.black, 0.0f, 0.15f, 0.0f);
        S("white level", &p.white, 0.85f, 1.0f, 1.0f);
        S("shadows", &p.shadows, -1.0f, 1.0f, 0.0f);
        S("highlights", &p.highlights, -1.0f, 1.0f, 0.0f);
        S("contrast", &p.contrast, 0.5f, 1.5f, 1.0f);
        S("film curve", &p.curve, 0.0f, 1.0f, 0.0f);
        S("gamma", &p.gamma, 0.6f, 1.6f, 1.0f);
        S("brightness", &p.brightness, -0.2f, 0.2f, 0.0f);
        break;
    case P_LENS:
        S("vignette", &p.vignette, 0.0f, 1.0f, 0.0f);
        S("film grain", &p.grain, 0.0f, 1.0f, 0.0f);
        S("chromatic", &p.chroma, 0.0f, 1.0f, 0.0f);
        S("scanlines", &p.scanlines, 0.0f, 1.0f, 0.0f);
        C("cinema bars", kBars, 4, &p.letterbox);
        break;
    case P_PRESETS:
        A("save preset", A_SAVE);
        A("save as new", A_SAVENEW);
        A("undo changes", A_UNDO);
        A("factory reset", A_FACTORY);
        break;
    case P_SETTINGS:
        T("fps counter", &ui.fps);
        S("compare split", &ui.comparePos, 0.1f, 0.9f, 0.5f);
        { static const Choice kSide[] = { { "left", 0.0f }, { "right", 1.0f } }; C("menu side", kSide, 2, &g_menuSide); }
        I("l1 + []   menu");
        I("r3 + left   effects on / off");
        I("r3 + right / up   next / prev preset");
        break;
    }
    return n;
}

bool Repeat(uint32_t held, uint32_t pressed, uint32_t bit, float dt, float& t) {
    if (!(held & bit)) { t = 0.0f; return false; }
    if (pressed & bit) { t = -0.35f; return true; }
    t += dt;
    if (t >= 0.05f) { t = 0.0f; return true; }
    return false;
}

bool Selectable(const Row& r) { return r.kind != K_INFO; }

void Close() { g_open = false; FxConfig::Save(); }

void HandleInput(Row* rows, int n, float dt) {
    static uint32_t s_prev = 0;
    static float tu = 0, td = 0, tl = 0, tr = 0;
    const uint32_t b = g_buttons, pressed = b & ~s_prev;
    s_prev = b;
    if (b & SCE_PAD_BUTTON_L1) return;
    if (pressed & SCE_PAD_BUTTON_CIRCLE) {
        if (g_page) { g_page = P_MAIN; g_sel = g_mainSel; }
        else Close();
        return;
    }
    int dir = 0;
    if (Repeat(b, pressed, SCE_PAD_BUTTON_UP, dt, tu)) dir = -1;
    if (Repeat(b, pressed, SCE_PAD_BUTTON_DOWN, dt, td)) dir = 1;
    if (dir) for (int k = 0; k < n; ++k) { g_sel = (g_sel + dir + n) % n; if (Selectable(rows[g_sel])) break; }
    int lr = 0;
    if (Repeat(b, pressed, SCE_PAD_BUTTON_LEFT, dt, tl)) lr = -1;
    if (Repeat(b, pressed, SCE_PAD_BUTTON_RIGHT, dt, tr)) lr = 1;
    const bool cross = (pressed & SCE_PAD_BUTTON_CROSS) != 0, tri = (pressed & SCE_PAD_BUTTON_TRIANGLE) != 0;
    Row& r = rows[g_sel];
    switch (r.kind) {
    case K_TOGGLE:
        if (lr || cross) {
            *r.flag = !*r.flag;
            if (r.flag == &g_fxOn) FxConfig::SetEnabled(g_fxOn);
            FxConfig::Save();
        }
        break;
    case K_PRESET:
        if (lr || cross) { FxConfig::SetPreset(FxConfig::PresetIndex() + (lr ? lr : 1)); FxConfig::Save(); WarmthFromTint(); }
        break;
    case K_SLIDER:
        if (lr) {
            *r.v += lr * (r.hi - r.lo) / 40.0f;
            if (*r.v < r.lo) *r.v = r.lo;
            if (*r.v > r.hi) *r.v = r.hi;
            if (r.v == &g_warmth) TintFromWarmth();
            if (r.v == &FxConfig::Ui().comparePos) FxConfig::Save();
        }
        if (tri) { *r.v = r.neutral; if (r.v == &g_warmth) TintFromWarmth(); }
        break;
    case K_CHOICE:
        if (lr || cross) {
            int cur = 0;
            for (int i = 0; i < r.nch; ++i) if (*r.cv == r.ch[i].value) cur = i;
            cur = (cur + (lr ? lr : 1) + r.nch) % r.nch;
            *r.cv = r.ch[cur].value;
            if (r.cv == &g_menuSide) { FxConfig::Ui().menuRight = g_menuSide > 0.5f; FxConfig::Save(); }
        }
        if (tri) *r.cv = r.ch[0].value;
        break;
    case K_PAGE:
        if (cross || lr > 0) { g_mainSel = g_sel; g_page = r.target; g_sel = 0; g_scroll = 0; }
        break;
    case K_ACTION:
        if (!cross) break;
        if (r.target == A_SAVE) Toast(FxConfig::SaveCurrent() ? "preset saved" : "save failed");
        else if (r.target == A_SAVENEW) {
            char nm[32] = "", m[64];
            if (FxConfig::SaveAsNew(nm, sizeof(nm))) { snprintf(m, sizeof(m), "saved as %s", nm); Toast(m); FxConfig::Save(); }
            else Toast("save failed");
        }
        else if (r.target == A_UNDO) { FxConfig::ResetCurrent(); WarmthFromTint(); Toast("changes undone"); }
        else if (r.target == A_FACTORY) {
            if (FxConfig::DefaultCurrent()) { WarmthFromTint(); Toast("factory values - save to keep"); }
            else Toast("only for built-in presets");
        }
        break;
    default: break;
    }
}

const ImU32 YEL = IM_COL32(0xF2, 0xC3, 0x18, 0xFF), INK = IM_COL32(0x14, 0x18, 0x1E, 0xFF), TXT = IM_COL32(0xE6, 0xE8, 0xEB, 0xFF),
            DIM = IM_COL32(0x7C, 0x85, 0x91, 0xFF), CYAN = IM_COL32(0x7F, 0xD8, 0xF0, 0xFF), GRN = IM_COL32(0x6E, 0xE0, 0x7A, 0xFF),
            PANEL = IM_COL32(0x10, 0x14, 0x18, 0xEB), LINE = IM_COL32(0x2A, 0x31, 0x3B, 0xFF);

void Text(ImDrawList* dl, float x, float y, float size, ImU32 c, const char* s) { dl->AddText(g_font, size, ImVec2(x, y), c, s); }
float TextW(float size, const char* s) { return g_font->CalcTextSizeA(size, 1e9f, 0.0f, s).x; }

void DrawMenu(Row* rows, int n, float cw) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int vis = 16;
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + vis) g_scroll = g_sel - vis + 1;
    const int shown = n < vis ? n : vis;
    const float w = 470.0f, rowH = 34.0f, ts = 23.0f, headH = 46.0f, bodyH = rowH * shown + 10.0f;
    const float x = FxConfig::Ui().menuRight ? cw - 54.0f - w : 54.0f, y = 96.0f, h = headH + bodyH + 30.0f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), PANEL);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 5.0f, y + h), YEL);
    Text(dl, x + 18.0f, y + 8.0f, 28.0f, YEL, "XeMoisties");
    char crumb[48]; snprintf(crumb, sizeof crumb, "fx / %s", kPageName[g_page]);
    Text(dl, x + 18.0f + TextW(28.0f, "XeMoisties") + 18.0f, y + 14.0f, 20.0f, DIM, crumb);
    dl->AddRectFilled(ImVec2(x + 14.0f, y + headH - 4.0f), ImVec2(x + w - 14.0f, y + headH - 2.0f), LINE);
    for (int i = 0; i < shown; ++i) {
        const int idx = g_scroll + i;
        const Row& r = rows[idx];
        const float ry = y + headH + rowH * i + 4.0f;
        const bool sel = idx == g_sel;
        if (sel) dl->AddRectFilled(ImVec2(x + 10.0f, ry), ImVec2(x + w - 10.0f, ry + rowH - 2.0f), YEL);
        Text(dl, x + 22.0f, ry + 4.0f, ts, sel ? INK : (r.kind == K_INFO ? DIM : TXT), r.label);
        char v[40] = ""; ImU32 vc = sel ? INK : CYAN;
        switch (r.kind) {
        case K_TOGGLE: snprintf(v, sizeof v, "%s", *r.flag ? "on" : "off"); if (!sel) vc = *r.flag ? GRN : DIM; break;
        case K_PAGE:   snprintf(v, sizeof v, ">"); if (!sel) vc = DIM; break;
        case K_PRESET: snprintf(v, sizeof v, "< %s >", FxConfig::Current().name); break;
        case K_SLIDER:
            snprintf(v, sizeof v, "< %.2f >", *r.v);
            if (!sel && *r.v == r.neutral) vc = DIM;
            break;
        case K_CHOICE: {
            const char* lab = r.ch[0].label;
            for (int k = 0; k < r.nch; ++k) if (*r.cv == r.ch[k].value) lab = r.ch[k].label;
            snprintf(v, sizeof v, "< %s >", lab);
            if (!sel && *r.cv == r.ch[0].value) vc = DIM;
            break;
        }
        default: break;
        }
        if (v[0]) Text(dl, x + w - 22.0f - TextW(ts, v), ry + 4.0f, ts, vc, v);
        if (r.kind == K_SLIDER) {
            const float t = (*r.v - r.lo) / (r.hi - r.lo), bx = x + 22.0f, bw = 150.0f, by = ry + rowH - 7.0f;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 2.0f), sel ? IM_COL32(0x14, 0x18, 0x1E, 90) : LINE);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * t, by + 2.0f), sel ? INK : YEL);
        }
    }
    if (n > vis) {
        char s[16]; snprintf(s, sizeof s, "%d/%d", g_sel + 1, n);
        Text(dl, x + w - 22.0f - TextW(18.0f, s), y + 16.0f, 18.0f, DIM, s);
    }
    const float fy = y + headH + bodyH + 2.0f;
    if (g_toastT > 0.0f) Text(dl, x + 18.0f, fy, 20.0f, GRN, g_toast);
    else Text(dl, x + 18.0f, fy, 19.0f, DIM, g_page == P_MAIN ? "X pick   <> change   O close" : "<> change   /\\ reset   X pick   O back");
}

void DrawOverlay(float cw, float ch, float dt, bool menu) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const FxUi& ui = FxConfig::Ui();
    if (ui.compare && FxConfig::Enabled()) {
        const float x = cw * ui.comparePos;
        dl->AddLine(ImVec2(x, 0.0f), ImVec2(x, ch), IM_COL32(255, 255, 255, 150), 2.0f);
        Text(dl, x - 14.0f - TextW(20.0f, "original"), ch - 42.0f, 20.0f, IM_COL32(255, 255, 255, 200), "original");
        Text(dl, x + 14.0f, ch - 42.0f, 20.0f, YEL, "xemofx");
    }
    if (ui.fps) {
        char s[24]; snprintf(s, sizeof s, "%.0f fps", g_fps);
        const float w = TextW(22.0f, s) + 24.0f, x = ui.menuRight && menu ? 24.0f : cw - w - 24.0f;
        dl->AddRectFilled(ImVec2(x, 24.0f), ImVec2(x + w, 58.0f), PANEL);
        dl->AddRectFilled(ImVec2(x, 24.0f), ImVec2(x + 4.0f, 58.0f), YEL);
        Text(dl, x + 14.0f, 28.0f, 22.0f, TXT, s);
    }
    if (g_toastT > 0.0f) {
        g_toastT -= dt;
        if (!menu) {
            const float w = TextW(22.0f, g_toast) + 36.0f, x = cw * 0.5f - w * 0.5f;
            dl->AddRectFilled(ImVec2(x, 40.0f), ImVec2(x + w, 78.0f), PANEL);
            dl->AddRectFilled(ImVec2(x, 40.0f), ImVec2(x + 4.0f, 78.0f), YEL);
            Text(dl, x + 18.0f, 45.0f, 22.0f, GRN, g_toast);
        }
    }
}

}

namespace Menu {

bool OnPad(uint32_t buttons) {
    static uint32_t s_prev = 0;
    const uint32_t pressed = buttons & ~s_prev;
    s_prev = buttons;
    if ((buttons & SCE_PAD_BUTTON_L1) && (pressed & SCE_PAD_BUTTON_SQUARE)) {
        g_open = !g_open;
        if (g_open) { g_fxOn = FxConfig::Enabled(); g_menuSide = FxConfig::Ui().menuRight ? 1.0f : 0.0f; WarmthFromTint(); }
        else FxConfig::Save();
    }
    g_buttons = buttons;
    return g_open;
}

bool IsOpen() { return g_open; }
float Split() { const FxUi& ui = FxConfig::Ui(); return ui.compare ? ui.comparePos : 0.0f; }
void SetFps(float fps) { g_fps = fps; }
void Notify(const char* msg) { Toast(msg); }

ImDrawData* Frame(uint32_t width, uint32_t height) {
    const FxUi& ui = FxConfig::Ui();
    if (!g_open && g_toastT <= 0.0f && !ui.fps && !(ui.compare && FxConfig::Enabled())) return nullptr;
    if (!g_inited) Init();
    ImGuiIO& io = ImGui::GetIO();

    if (height > 1080) io.DisplaySize = ImVec2((float)width * 1080.0f / (float)height, 1080.0f);
    else io.DisplaySize = ImVec2((float)width, (float)height);
    static uint64_t s_last = 0;
    const uint64_t now = sceKernelGetProcessTime();
    float dt = s_last ? (float)(now - s_last) / 1000000.0f : (1.0f / 60.0f);
    if (dt < 0.001f) dt = 0.001f; if (dt > 0.1f) dt = 0.1f;
    s_last = now; io.DeltaTime = dt;
    Row rows[24];
    const int n = BuildRows(g_page, rows);
    if (g_sel >= n) g_sel = 0;
    while (!Selectable(rows[g_sel])) g_sel = (g_sel + 1) % n;
    const bool menu = g_open;
    if (g_open) HandleInput(rows, n, dt);
    ImGui::NewFrame();
    if (g_open) { const int n2 = BuildRows(g_page, rows); DrawMenu(rows, n2, io.DisplaySize.x); }
    DrawOverlay(io.DisplaySize.x, io.DisplaySize.y, dt, menu);
    ImGui::Render();
    return ImGui::GetDrawData();
}

}
