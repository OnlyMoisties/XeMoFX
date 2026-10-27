#pragma once
#include <stdint.h>
#include <stddef.h>

#define FX_DIR "/data/GoldHEN/XeMoFX"

void FxLog(const char* fmt, ...);
void FxNotify(const char* fmt, ...);
const char* FxTitleId();


void* FxResolve(const char* mod, const char* sym);

int FxPatchImports(void* const* orig, void* const* hook, int n);
