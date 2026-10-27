#pragma once
#include <stdint.h>

struct ImDrawData;

namespace Menu {

bool OnPad(uint32_t buttons);
bool IsOpen();

ImDrawData* Frame(uint32_t width, uint32_t height);

float Split();
void SetFps(float fps);
void Notify(const char* msg);

}
