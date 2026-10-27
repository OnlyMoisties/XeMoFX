#pragma once

struct FxParams {
    float sharpen;
    float clarity;
    float curve;
    float contrast;
    float brightness;
    float gamma;
    float saturation;
    float vibrance;
    float tint[3];
    float vignette;
    float grain;
    float black;
    float white;
    float exposure;
    float shadows;
    float highlights;
    float tealOrange;
    float tintGM;
    float glow;
    float chroma;
    float letterbox;
    float scanlines;
    float cbMode;
    float deband;
};

struct FxUi {
    bool  compare;
    float comparePos;
    bool  fps;
    bool  menuRight;
};

struct FxPreset {
    char     name[32];
    char     file[64];
    FxParams p;
};

namespace FxConfig {

void Load();
bool Enabled();
void SetEnabled(bool on);
int  PresetCount();
int  PresetIndex();
const char* PresetName(int idx);
void SetPreset(int idx);
const FxPreset& Current();
FxParams& Edit();
bool SaveCurrent();
bool SaveAsNew(char* nameOut, int cap);
void ResetCurrent();
bool DefaultCurrent();
FxUi& Ui();

void Save();
}
