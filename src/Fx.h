#pragma once
#include <stdint.h>
#include "Config.h"

struct ImDrawData;

namespace Fx {

struct FlipArgs {
    uint32_t  count;
    void**    dcb;  uint32_t* dcbSizes;
    void**    ccb;  uint32_t* ccbSizes;
    uint32_t  voHandle, dispIdx, flipMode;
    int64_t   flipArg;
    void*     flipFn;
    void*     wlFn;
    uint64_t  workload;
};

void RegisterBuffers(int startIndex, void* const* addrs, int num, const uint32_t* attr);
void DisplaySize(uint32_t& w, uint32_t& h);

void ObserveSubmit(uint32_t count, void* const* dcb, const uint32_t* dcbSizes);

int  Present(const FlipArgs& fa, const FxParams* fx, float split, ImDrawData* ui);

}
