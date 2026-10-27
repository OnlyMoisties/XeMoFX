#include "Fx.h"
#include "Util.h"
#include <gnm.h>
#include <gnmx.h>
#include <gnmx/shader_parser.h>
#include <gpu_address.h>
#include <kernel.h>
#include <string.h>
#include <stdio.h>
#include "../third_party/imgui/imgui.h"

#include "../Shaders/fx_shaders.h"

using namespace sce;

namespace {

struct DMemBlock {
    uint8_t* base = nullptr;
    off_t    phys = 0;
    size_t   size = 0;
    size_t   used = 0;
    bool init(size_t bytes, int memType) {
        const size_t kAlign = 2 * 1024 * 1024;
        size = (bytes + kAlign - 1) & ~(kAlign - 1);
        int r = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, kAlign, memType, &phys);
        if (r != 0) { FxLog("[FX] AllocateDirectMemory failed 0x%x (type %d, %zu B)", r, memType, size); return false; }
        void* addr = nullptr;
        r = sceKernelMapDirectMemory(&addr, size, SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_WRITE | SCE_KERNEL_PROT_GPU_ALL, 0, phys, kAlign);
        if (r != 0) { FxLog("[FX] MapDirectMemory failed 0x%x", r); sceKernelReleaseDirectMemory(phys, size); return false; }
        base = (uint8_t*)addr;
        return true;
    }
    void* alloc(size_t bytes, size_t align) {
        size_t p = (used + align - 1) & ~(align - 1);
        if (!base || p + bytes > size) return nullptr;
        used = p + bytes;
        return base + p;
    }
};

DMemBlock g_garlic, g_onion, g_scratch;

Gnmx::VsShader* g_vs = nullptr;
Gnmx::PsShader* g_psTex = nullptr;
Gnmx::PsShader* g_psBuf = nullptr;
Gnmx::VsShader* g_uiVs = nullptr;
Gnmx::PsShader* g_uiPs = nullptr;
uint32_t g_uiPsUsage[32]; uint32_t g_uiPsUsageN = 0;

Gnm::Texture g_fontTex; bool g_fontReady = false;
Gnm::Sampler g_sampler;

const int kFrames = 4;
const uint32_t kDcbBytes = 256 * 1024;
const uint32_t kCombinedBytes = 6 * 1024 * 1024;
const uint32_t kVtxBytes = 512 * 1024, kIdxBytes = 256 * 1024;
struct Frame {
    Gnm::DrawCommandBuffer dcb;
    uint8_t* combined = nullptr;
    float*   cb = nullptr;
    uint8_t* vtx = nullptr; uint8_t* idx = nullptr;
    volatile uint64_t* label = nullptr;
    uint64_t tag = 0;
};
Frame g_frame[kFrames];

bool     g_ready = false, g_initTried = false, g_off = false;
uint32_t g_flipBytes = 0, g_flipHdr = 0;

void*    g_disp[16] = {};
uint32_t g_vfmt = 0, g_vtiling = 0, g_w = 0, g_h = 0;

bool     g_rtCached = false;
uint32_t g_rtRegs[6];

template <typename T> T* CopyShader(const void* hdr, uint32_t hdrSize, const Gnmx::ShaderInfo& si) {
    T* s = (T*)g_onion.alloc(hdrSize, Gnm::kAlignmentOfBufferInBytes);
    void* isa = g_garlic.alloc(si.m_gpuShaderCodeSize, Gnm::kAlignmentOfShaderInBytes);
    if (!s || !isa) return nullptr;
    memcpy(s, hdr, hdrSize);
    memcpy(isa, si.m_gpuShaderCode, si.m_gpuShaderCodeSize);
    s->patchShaderGpuAddress(isa);
    return s;
}

bool ImmOnly(const Gnm::InputUsageSlot* u, uint32_t n, const char* what) {
    for (uint32_t i = 0; i < n; ++i)
        if (u[i].m_usageType != Gnm::kShaderInputUsageImmResource && u[i].m_usageType != Gnm::kShaderInputUsageImmConstBuffer &&
            u[i].m_usageType != Gnm::kShaderInputUsageImmSampler) {
            FxLog("[FX] %s input slot %u has usage 0x%x - unsupported", what, i, u[i].m_usageType);
            return false;
        }
    return true;
}
Gnmx::VsShader* LoadVs(const void* sb) {
    Gnmx::ShaderInfo si; Gnmx::parseShader(&si, sb);
    Gnmx::VsShader* vs = CopyShader<Gnmx::VsShader>(si.m_vsShader, si.m_vsShader->computeSize(), si);
    return (vs && ImmOnly(vs->getInputUsageSlotTable(), vs->m_common.m_numInputUsageSlots, "vs")) ? vs : nullptr;
}
Gnmx::PsShader* LoadPs(const void* sb) {
    Gnmx::ShaderInfo si; Gnmx::parseShader(&si, sb);
    Gnmx::PsShader* ps = CopyShader<Gnmx::PsShader>(si.m_psShader, si.m_psShader->computeSize(), si);
    return (ps && ImmOnly(ps->getInputUsageSlotTable(), ps->m_common.m_numInputUsageSlots, "ps")) ? ps : nullptr;
}

struct Bindings { const Gnm::Texture* tex; const Gnm::Buffer* buf; const Gnm::Buffer* cb; const Gnm::Sampler* smp; };
void Bind(Gnm::DrawCommandBuffer& d, Gnm::ShaderStage stage, const Gnm::InputUsageSlot* u, uint32_t n, const Bindings& b) {
    for (uint32_t i = 0; i < n; ++i) {
        if (u[i].m_usageType == Gnm::kShaderInputUsageImmConstBuffer) d.setVsharpInUserData(stage, u[i].m_startRegister, b.cb);
        else if (u[i].m_usageType == Gnm::kShaderInputUsageImmSampler) d.setSsharpInUserData(stage, u[i].m_startRegister, b.smp);
        else if (u[i].m_resourceType)                                 d.setTsharpInUserData(stage, u[i].m_startRegister, b.tex);
        else                                                          d.setVsharpInUserData(stage, u[i].m_startRegister, b.buf);
    }
}

bool Init() {
    if (g_ready) return true;
    if (g_initTried) return false;
    g_initTried = true;
    if (!g_garlic.init(4u * 1024 * 1024 + kFrames * (kVtxBytes + kIdxBytes), SCE_KERNEL_WC_GARLIC)) return false;
    if (!g_onion.init(kFrames * (kDcbBytes + kCombinedBytes) + 1024 * 1024, SCE_KERNEL_WB_ONION)) return false;

    g_vs = LoadVs(g_fx_vsSb);
    g_psTex = LoadPs(g_fx_ps_texSb);
    g_psBuf = LoadPs(g_fx_ps_bufSb);
    g_uiVs = LoadVs(g_ui_vsSb);
    g_uiPs = LoadPs(g_ui_psSb);
    if (!g_vs || !g_psTex || !g_psBuf || !g_uiVs || !g_uiPs) { FxLog("[FX] shader setup failed"); return false; }
    Gnm::generatePsShaderUsageTable(g_uiPsUsage, g_uiVs->getExportSemanticTable(), g_uiVs->m_numExportSemantics,
                                    g_uiPs->getPixelInputSemanticTable(), g_uiPs->m_numInputSemantics);
    g_uiPsUsageN = g_uiPs->m_numInputSemantics;

    g_sampler.init();
    g_sampler.setMipFilterMode(Gnm::kMipFilterModeNone);
    g_sampler.setXyFilterMode(Gnm::kFilterModeBilinear, Gnm::kFilterModeBilinear);
    g_sampler.setWrapMode(Gnm::kWrapModeClampLastTexel, Gnm::kWrapModeClampLastTexel, Gnm::kWrapModeClampLastTexel);

    for (int i = 0; i < kFrames; ++i) {
        Frame& f = g_frame[i];
        void* mem  = g_onion.alloc(kDcbBytes, Gnm::kAlignmentOfBufferInBytes);
        f.combined = (uint8_t*)g_onion.alloc(kCombinedBytes, Gnm::kAlignmentOfBufferInBytes);
        f.label    = (volatile uint64_t*)g_onion.alloc(8, 8);
        f.cb       = (float*)g_garlic.alloc(256, Gnm::kAlignmentOfBufferInBytes);
        f.vtx      = (uint8_t*)g_garlic.alloc(kVtxBytes, Gnm::kAlignmentOfBufferInBytes);
        f.idx      = (uint8_t*)g_garlic.alloc(kIdxBytes, Gnm::kAlignmentOfBufferInBytes);
        if (!mem || !f.combined || !f.label || !f.cb || !f.vtx || !f.idx) return false;
        f.dcb.init(mem, kDcbBytes, nullptr, nullptr);
        *f.label = 0;
    }
    {
        Gnm::DrawCommandBuffer& d = g_frame[0].dcb;
        d.resetBuffer();
        d.prepareFlip();
        g_flipBytes = d.getSizeInBytes();
        g_flipHdr = d.m_beginptr[0];
        d.resetBuffer();
    }
    g_ready = true;
    FxLog("[FX] ready (prepareFlip packet %u B, hdr 0x%08x)", g_flipBytes, g_flipHdr);
    return true;
}

bool EnsureFont() {
    if (g_fontReady) return true;
    unsigned char* px = nullptr; int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    if (!px || w <= 0 || h <= 0) return false;
    Gnm::SizeAlign sa = g_fontTex.initAs2d(w, h, 1, Gnm::kDataFormatR8G8B8A8Unorm, Gnm::kTileModeDisplay_LinearAligned, Gnm::kNumFragments1);
    uint8_t* mem = (uint8_t*)g_garlic.alloc(sa.m_size, sa.m_align ? sa.m_align : 256);
    if (!mem) { FxLog("[FX] no room for the font atlas"); return false; }
    const uint32_t pitch = g_fontTex.getPitch();
    for (int y = 0; y < h; ++y) memcpy(mem + (size_t)y * pitch * 4, px + (size_t)y * w * 4, (size_t)w * 4);
    g_fontTex.setBaseAddress(mem);
    g_fontTex.setResourceMemoryType(Gnm::kResourceMemoryTypeRO);
    ImGui::GetIO().Fonts->SetTexID((ImTextureID)(intptr_t)&g_fontTex);
    g_fontReady = true;
    return true;
}

bool FindRtRegs(const uint32_t* dcb, uint32_t dwords, uint32_t base256, uint32_t regs[6]) {
    if (!dcb) return false;
    for (uint32_t i = 0; i + 1 < dwords; ) {
        uint32_t hdr = dcb[i];
        if ((hdr >> 30) != 3u) { i++; continue; }
        uint32_t cnt = ((hdr >> 16) & 0x3FFFu) + 1u;
        if (((hdr >> 8) & 0xFFu) == 0x69u && cnt >= 2u) {
            uint32_t first = dcb[i + 1] & 0xFFFFu, nregs = cnt - 1u;
            if (first <= 0x318u && first + nregs > 0x31Du && i + 2 + (0x31Du - first) < dwords &&
                dcb[i + 2 + (0x318u - first)] == base256) {
                for (int k = 0; k < 6; k++) regs[k] = dcb[i + 2 + (0x318u - first) + k];
                return true;
            }
        }
        i += 1u + cnt;
    }
    return false;
}

void LookForRegs(uint32_t count, void* const* dcb, const uint32_t* sizes, const char* where) {
    if (g_rtCached || !dcb || !sizes) return;
    for (int d = (int)count - 1; d >= 0; --d)
        for (int b = 0; b < 16; ++b) {
            uint32_t regs[6];
            if (g_disp[b] && dcb[d] && sizes[d] &&
                FindRtRegs((const uint32_t*)dcb[d], sizes[d] / 4, (uint32_t)((uintptr_t)g_disp[b] >> 8), regs)) {
                memcpy(g_rtRegs, regs, sizeof(g_rtRegs));
                g_rtCached = true;
                FxLog("[FX] display RT regs (%s): pitch 0x%x slice 0x%x info 0x%x attrib 0x%x (tile %u)",
                      where, regs[1], regs[2], regs[4], regs[5], (regs[5] >> 4) & 0x1Fu);
                return;
            }
        }
}

bool FallbackRegs(uint32_t regs[6], bool tenBit) {
    static int s_neo = -1;
    if (s_neo < 0) {
        void* fn = nullptr;
        s_neo = (sceKernelDlsym(0x2001, "sceKernelIsNeoMode", &fn) == 0 && fn) ? (((int (*)())fn)() ? 1 : 0) : (g_w > 1920 ? 1 : 0);
        FxLog("[FX] no display RT regs seen - computed layout, Pro (Neo) mode %d", s_neo);
    }
    const Gnm::DataFormat fmt = tenBit ? Gnm::kDataFormatB10G10R10A2Unorm : Gnm::kDataFormatB8G8R8A8Unorm;
    Gnm::TileMode tile;
    if (s_neo) tile = (Gnm::TileMode)20;
    else if (g_vtiling == 1) tile = Gnm::kTileModeDisplay_LinearAligned;
    else GpuAddress::computeSurfaceTileMode(&tile, GpuAddress::kSurfaceTypeColorTargetDisplayable, fmt, 1);
    Gnm::RenderTarget rt;
    rt.init(g_w, g_h, 1, fmt, tile, Gnm::kNumSamples1, Gnm::kNumFragments1, nullptr, nullptr);
    for (int k = 0; k < 6; k++) regs[k] = rt.m_regs[k];
    if (s_neo) {
        if (tenBit) return false;
        regs[4] = 0x80008828u; regs[5] = 0x14au;
    }
    return true;
}

void Refuse(const char* why) {
    if (!g_off) { FxLog("[FX] drawing off in this game: %s", why); FxNotify("XeMoFX can't draw in this game:\n%s", why); }
    g_off = true;
}

void RecordFx(Frame& f, Gnm::DrawCommandBuffer& d, const FxParams& in, float split, bool proLayout, bool tenBit,
              uint32_t pitchPx, uint64_t bytes, void* target, const uint32_t regs[6], Gnm::RenderTarget& rt) {
    const FxParams& p = in;
    Gnm::Texture tex; Gnm::Buffer buf;
    if (proLayout) { buf.initAsDataBuffer(g_scratch.base, Gnm::kDataFormatR32Uint, (uint32_t)(bytes / 4)); buf.setResourceMemoryType(Gnm::kResourceMemoryTypeRO); }
    else { tex.initFromRenderTarget(&rt, false); tex.setBaseAddress(g_scratch.base); tex.setResourceMemoryType(Gnm::kResourceMemoryTypeRO); }

    float* c = f.cb;
    const float t = (float)(sceKernelGetProcessTime() % 100000000ull) / 1000000.0f;
    c[0] = p.sharpen;    c[1] = p.contrast; c[2] = p.saturation; c[3] = p.vibrance;
    c[4] = p.brightness; c[5] = p.gamma;    c[6] = p.vignette;   c[7] = p.grain;
    c[8] = p.tint[0];    c[9] = p.tint[1] * (1.0f + 0.06f * p.tintGM); c[10] = p.tint[2]; c[11] = t;
    uint32_t* u = (uint32_t*)(c + 12);
    u[0] = g_w; u[1] = g_h; u[2] = pitchPx / 128u; u[3] = ((tenBit && proLayout) ? 1u : 0u) | (p.deband > 0.5f ? 4u : 0u);
    c[16] = p.clarity;   c[17] = p.curve;   c[18] = p.black;     c[19] = p.white;
    c[20] = p.exposure;  c[21] = p.shadows; c[22] = p.highlights; c[23] = p.tealOrange;
    c[24] = p.glow;      c[25] = p.chroma;  c[26] = p.letterbox; c[27] = p.scanlines;
    c[28] = p.cbMode;    c[29] = split;     c[30] = 0;           c[31] = 0;
    Gnm::Buffer cb; cb.initAsConstantBuffer(f.cb, 128); cb.setResourceMemoryType(Gnm::kResourceMemoryTypeRO);

    d.waitForGraphicsWrites(regs[0], (uint32_t)((bytes + 255) >> 8), Gnm::kWaitTargetSlotCb0,
                            Gnm::kCacheActionWriteBackAndInvalidateL1andL2, Gnm::kExtendedCacheActionFlushAndInvalidateCbCache,
                            Gnm::kStallCommandBufferParserEnable);
    const uint32_t kChunk = 1024 * 1024;
    for (uint64_t off = 0; off < bytes; off += kChunk) {
        const uint32_t n = (uint32_t)((bytes - off) < kChunk ? (bytes - off) : kChunk);
        d.dmaData(Gnm::kDmaDataDstMemory, (uint64_t)(uintptr_t)g_scratch.base + off, Gnm::kDmaDataSrcMemory,
                  (uint64_t)(uintptr_t)target + off, n, Gnm::kDmaDataBlockingEnable);
    }
    d.flushShaderCachesAndWait(Gnm::kCacheActionWriteBackAndInvalidateL1andL2, 0, Gnm::kStallCommandBufferParserEnable);

    { Gnm::BlendControl bc; bc.init(); bc.setBlendEnable(false); d.setBlendControl(0, bc); }
    d.setVsShader(&g_vs->m_vsStageRegisters, 0);
    const Gnmx::PsShader* ps = proLayout ? g_psBuf : g_psTex;
    d.setPsShader(&ps->m_psStageRegisters);
    const Bindings b = { &tex, &buf, &cb, &g_sampler };
    Bind(d, Gnm::kShaderStagePs, ps->getInputUsageSlotTable(), ps->m_common.m_numInputUsageSlots, b);
    d.drawIndexAuto(3);
}

void RecordUi(Frame& f, Gnm::DrawCommandBuffer& d, ImDrawData* dd) {
    if (!EnsureFont()) return;
    const float cw = dd->DisplaySize.x > 0 ? dd->DisplaySize.x : (float)g_w, ch = dd->DisplaySize.y > 0 ? dd->DisplaySize.y : (float)g_h;
    const float sx = (float)g_w / cw, sy = (float)g_h / ch;
    float* xf = f.cb + 32;
    xf[0] = 2.0f / cw; xf[1] = 2.0f / ch; xf[2] = 0; xf[3] = 0;
    Gnm::Buffer cb; cb.initAsConstantBuffer(xf, 16); cb.setResourceMemoryType(Gnm::kResourceMemoryTypeRO);

    Gnm::BlendControl bc; bc.init(); bc.setBlendEnable(true);
    bc.setColorEquation(Gnm::kBlendMultiplierSrcAlpha, Gnm::kBlendFuncAdd, Gnm::kBlendMultiplierOneMinusSrcAlpha);
    bc.setSeparateAlphaEnable(true);
    bc.setAlphaEquation(Gnm::kBlendMultiplierOne, Gnm::kBlendFuncAdd, Gnm::kBlendMultiplierOneMinusSrcAlpha);
    d.setBlendControl(0, bc);
    d.setVsShader(&g_uiVs->m_vsStageRegisters, 0);
    d.setPsShader(&g_uiPs->m_psStageRegisters);
    if (g_uiPsUsageN) d.setPsShaderUsage(g_uiPsUsage, g_uiPsUsageN);
    d.setIndexSize(sizeof(ImDrawIdx) == 2 ? Gnm::kIndexSize16 : Gnm::kIndexSize32);
    d.setIndexOffset(0);

    uint8_t* vw = f.vtx; uint8_t* iw = f.idx;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        const size_t vb = (size_t)cl->VtxBuffer.Size * sizeof(ImDrawVert), ib = (size_t)cl->IdxBuffer.Size * sizeof(ImDrawIdx);
        if (vw + vb > f.vtx + kVtxBytes || iw + ib > f.idx + kIdxBytes) break;
        memcpy(vw, cl->VtxBuffer.Data, vb);
        memcpy(iw, cl->IdxBuffer.Data, ib);
        Gnm::Buffer verts; verts.initAsRegularBuffer(vw, sizeof(ImDrawVert), (uint32_t)cl->VtxBuffer.Size);
        verts.setResourceMemoryType(Gnm::kResourceMemoryTypeRO);
        const Bindings vbnd = { nullptr, &verts, &cb, nullptr };
        Bind(d, Gnm::kShaderStageVs, g_uiVs->getInputUsageSlotTable(), g_uiVs->m_common.m_numInputUsageSlots, vbnd);
        for (int c = 0; c < cl->CmdBuffer.Size; ++c) {
            const ImDrawCmd& dc = cl->CmdBuffer[c];
            if (dc.UserCallback) continue;
            int x0 = (int)(dc.ClipRect.x * sx), y0 = (int)(dc.ClipRect.y * sy);
            int x1 = (int)(dc.ClipRect.z * sx + 0.999f), y1 = (int)(dc.ClipRect.w * sy + 0.999f);
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 > (int)g_w) x1 = (int)g_w; if (y1 > (int)g_h) y1 = (int)g_h;
            if (x1 <= x0 || y1 <= y0) continue;
            Gnm::Texture* t = (Gnm::Texture*)(uintptr_t)dc.GetTexID();
            const Bindings pbnd = { t ? t : &g_fontTex, nullptr, nullptr, &g_sampler };
            Bind(d, Gnm::kShaderStagePs, g_uiPs->getInputUsageSlotTable(), g_uiPs->m_common.m_numInputUsageSlots, pbnd);
            d.setScreenScissor(x0, y0, x1, y1);
            d.drawIndex(dc.ElemCount, iw + dc.IdxOffset * sizeof(ImDrawIdx));
        }
        vw += (vb + 255) & ~(size_t)255;
        iw += (ib + 255) & ~(size_t)255;
    }
}

}

namespace Fx {

void RegisterBuffers(int startIndex, void* const* addrs, int num, const uint32_t* attr) {
    for (int i = 0; i < num && startIndex + i < 16; ++i) g_disp[startIndex + i] = addrs[i];
    if (attr) { g_vfmt = attr[0]; g_vtiling = attr[1]; g_w = attr[3]; g_h = attr[4]; }
    FxLog("[FX] display buffers: start %d num %d fmt 0x%x tiling %u %ux%u", startIndex, num, g_vfmt, g_vtiling, g_w, g_h);
}

void DisplaySize(uint32_t& w, uint32_t& h) { w = g_w; h = g_h; }

void ObserveSubmit(uint32_t count, void* const* dcb, const uint32_t* dcbSizes) {
    static uint32_t s_seen = 0;
    if (g_rtCached || s_seen > 4000) return;
    ++s_seen;
    LookForRegs(count, dcb, dcbSizes, "plain submit");
}

int Present(const FlipArgs& fa, const FxParams* fx, float split, ImDrawData* ui) {
    if (ui && ui->CmdListsCount == 0) ui = nullptr;
    if ((!fx && !ui) || g_off) return -1;
    { static bool s_l = false; if (!s_l) { s_l = true; FxLog("[FX] first flip: %u dcbs, dispIdx %u, buf %p, %ux%u, wl %d", fa.count, fa.dispIdx,
                                                             fa.dispIdx < 16 ? g_disp[fa.dispIdx] : nullptr, g_w, g_h, fa.wlFn ? 1 : 0); } }
    if (fa.count < 1 || fa.count > 16 || fa.dispIdx >= 16 || !g_disp[fa.dispIdx] || !g_w || !g_h) return -1;
    if (!Init()) { Refuse("init failed (no free GPU memory)"); return -1; }
    void* target = g_disp[fa.dispIdx];
    Frame& f = g_frame[fa.dispIdx % kFrames];
    if (f.tag && *f.label != f.tag) return -1;

    LookForRegs(fa.count, fa.dcb, fa.dcbSizes, "flip");
    const bool tenBit = (g_vfmt >> 24) == 0x88u;
    uint32_t regs[6];
    if (g_rtCached) memcpy(regs, g_rtRegs, sizeof(regs));
    else if (!FallbackRegs(regs, tenBit)) { Refuse("unmeasured Pro 10-bit layout"); return -1; }
    regs[0] = (uint32_t)((uintptr_t)target >> 8);
    regs[4] &= ~(7u << 8);
    const uint32_t cbFmt = (regs[4] >> 2) & 0x1Fu;
    const bool proLayout = (regs[4] >> 31) != 0;
    const uint32_t tile = (regs[5] >> 4) & 0x1Fu;
    if (cbFmt != 0xA && cbFmt != 0x9 && cbFmt != 0x8) { Refuse("display format isn't 32bpp"); return -1; }
    if (proLayout && (tile != 20 || cbFmt != 0xA)) { Refuse("unmeasured Pro display layout"); return -1; }
    const uint32_t pitchPx = ((regs[1] & 0x7FFu) + 1u) * 8u;
    const uint64_t bytes = (uint64_t)((regs[2] & 0x3FFFFFu) + 1u) * 64u * 4u;

    if (fx && !g_scratch.base) {
        static bool s_noMem = false;

        const uint64_t padded = (uint64_t)pitchPx * ((g_h + 127u) & ~127u) * 4u;
        if (s_noMem || !g_scratch.init((size_t)(bytes > padded ? bytes : padded), SCE_KERNEL_WC_GARLIC)) {
            if (!s_noMem) { s_noMem = true; FxNotify("XeMoFX: not enough free memory in this game for effects"); }
            fx = nullptr;
        } else FxLog("[FX] frame copy %llu B, %s read, pitch %u, %s", (unsigned long long)bytes, proLayout ? "Pro buffer" : "texture", pitchPx, tenBit ? "10-bit" : "8-bit");
    }
    if (fx && bytes > g_scratch.size) {
        static bool s_l = false; if (!s_l) { s_l = true; FxLog("[FX] frame copy too small (%llu > %zu) - effect skipped", (unsigned long long)bytes, g_scratch.size); }
        fx = nullptr;
    }
    if (!fx && !ui) return -1;

    Gnm::RenderTarget rt;
    rt.init(g_w, g_h, 1, tenBit ? Gnm::kDataFormatB10G10R10A2Unorm : Gnm::kDataFormatB8G8R8A8Unorm,
            (Gnm::TileMode)tile, Gnm::kNumSamples1, Gnm::kNumFragments1, nullptr, nullptr);
    for (int k = 0; k < 6; k++) rt.m_regs[k] = regs[k];

    Gnm::DrawCommandBuffer& d = f.dcb;
    d.resetBuffer();
    d.initializeDefaultHardwareState();
    d.setRenderTarget(0, &rt);
    d.setRenderTargetMask(0xF);
    d.setDepthRenderTarget(nullptr);
    { Gnm::DepthStencilControl ds; ds.init(); d.setDepthStencilControl(ds); }
    { Gnm::PrimitiveSetup ps; ps.init(); d.setPrimitiveSetup(ps); }
    { Gnm::ClipControl cc; cc.init(); d.setClipControl(cc); }
    Gnmx::setupScreenViewport(&d, 0, 0, g_w, g_h, 0.5f, 0.5f);
    d.setScreenScissor(0, 0, (int32_t)g_w, (int32_t)g_h);
    d.setActiveShaderStages(Gnm::kActiveShaderStagesVsPs);
    d.setPrimitiveType(Gnm::kPrimitiveTypeTriList);
    d.setNumInstances(1);
    if (fx) RecordFx(f, d, *fx, split, proLayout, tenBit, pitchPx, bytes, target, regs, rt);
    if (ui) { d.setScreenScissor(0, 0, (int32_t)g_w, (int32_t)g_h); RecordUi(f, d, ui); }

    static uint64_t s_tag = 0;
    const uint64_t tag = ++s_tag;
    d.writeAtEndOfPipe(Gnm::kEopFlushCbDbCaches, Gnm::kEventWriteDestMemory, (void*)f.label,
                       Gnm::kEventWriteSource64BitsImmediate, tag, Gnm::kCacheActionWriteBackAndInvalidateL1andL2, Gnm::kCachePolicyLru);
    const uint32_t ourSz = d.getSizeInBytes();

    const uint8_t* last = (const uint8_t*)fa.dcb[fa.count - 1];
    const uint32_t lastSz = fa.dcbSizes[fa.count - 1];
    const uint32_t fin = (g_flipBytes && lastSz > g_flipBytes) ? lastSz - g_flipBytes : 0;
    if (!fin || *(const uint32_t*)(last + fin) != g_flipHdr) {
        char why[96]; snprintf(why, sizeof(why), "unusual flip (tail 0x%08x, mode %u)", fin ? *(const uint32_t*)(last + fin) : 0u, fa.flipMode);
        Refuse(why);
        return -1;
    }
    const uint32_t total = fin + ourSz + g_flipBytes;
    if (total > kCombinedBytes) { static bool s_l = false; if (!s_l) { s_l = true; FxLog("[FX] flip dcb too big (%u B) - frame skipped", lastSz); } return -1; }
    memcpy(f.combined, last, fin);
    memcpy(f.combined + fin, d.m_beginptr, ourSz);
    memcpy(f.combined + fin + ourSz, last + fin, g_flipBytes);

    void* dcbs[16]; uint32_t dsz[16]; void* ccbs[16] = {}; uint32_t csz[16] = {};
    memcpy(dcbs, fa.dcb, fa.count * sizeof(void*));
    memcpy(dsz, fa.dcbSizes, fa.count * sizeof(uint32_t));
    if (fa.ccb) memcpy(ccbs, fa.ccb, fa.count * sizeof(void*));
    if (fa.ccbSizes) memcpy(csz, fa.ccbSizes, fa.count * sizeof(uint32_t));
    dcbs[fa.count - 1] = f.combined; dsz[fa.count - 1] = total;

    typedef int (*FlipFn)(uint32_t, void**, uint32_t*, void**, uint32_t*, uint32_t, uint32_t, uint32_t, int64_t);
    typedef int (*FlipWLFn)(uint64_t, uint32_t, void**, uint32_t*, void**, uint32_t*, uint32_t, uint32_t, uint32_t, int64_t);
    const int r = fa.wlFn ? ((FlipWLFn)fa.wlFn)(fa.workload, fa.count, dcbs, dsz, ccbs, csz, fa.voHandle, fa.dispIdx, fa.flipMode, fa.flipArg)
                          : ((FlipFn)fa.flipFn)(fa.count, dcbs, dsz, ccbs, csz, fa.voHandle, fa.dispIdx, fa.flipMode, fa.flipArg);
    { static int s_n = 0; if (s_n < 3 || r != 0) { if (s_n < 50) FxLog("[FX] frame: %u dcbs, ours %u B (fx %d ui %d), total %u B, r=0x%x", fa.count, ourSz, fx ? 1 : 0, ui ? 1 : 0, total, r); s_n++; } }
    if (r != 0) return -1;
    f.tag = tag;
    return 0;
}

}
