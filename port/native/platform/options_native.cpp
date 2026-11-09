// The front end's Options item in the native build: a plain settings screen drawn over the
// menu (render/gles_renderer.cpp's overlay), in place of the original's separate GTA2
// Manager program (winmain.cpp Start_GTA2Manager_5E4DE0, patch 0024: the browser shows its
// Controls screen instead).
//
// Each line is one of the settings GTA2 Manager wrote to the registry, applied at once
// where the game allows it and saved with the registry (conf/registry.txt):
//   music and radio volume   sound\cdvol, 0..127            at once (root_sound)
//   effects volume           sound\sfxvol, 0..127           at once
//   text speed               option\text_speed, 1..5        when a game starts (Hud_2B00)
//   exploding scores         screen\exploding_on            at once (Init_FrameRateLight...)
//   lighting                 screen\lighting                at once (same)
// plus the port's own controller setting:
//   face buttons             controls.ini face_buttons      at once (input_sdl.cpp)
//
// It opens from the main menu's Options, and in the city from the pause (Start, then Select:
// input_sdl.cpp) or F8 (the original's GTA2 Manager key). The original's quit prompt does
// not pause, so the screen pauses a single-player game while it is open (Game_0x40's pause,
// as F6); a network game runs on, as the other players' do.
// Up/down choose a line, left/right change it (A too), B or Start close; with a keyboard,
// the arrows, Return and Escape. input_sdl.cpp sends the presses here while the screen is
// open, and none to the game.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "Game_0x40.hpp"
#include "cSampleManager.hpp"
#include "debug.hpp"
#include "winmain.hpp"
#include "root_sound.hpp"
#include "sound_obj.hpp"

void __stdcall Init_FrameRateLightAndUnknown_5D8EB0(); // winmain.cpp: reads lighting, exploding_on

extern "C" void gta2_port_reg_set_dword(const char* path, const char* name, DWORD value);
extern "C" unsigned int Gta2Native_RegistryDword(const char* path, const char* name, unsigned int fallback);
extern "C" void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba);
extern "C" void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);
extern "C" bool Gta2Native_IsPlaying();          // status_native.cpp
extern "C" int Gta2Native_FaceButtons();          // input_sdl.cpp: 0 auto, 1 nintendo, 2 xbox
extern "C" void Gta2Native_SetFaceButtons(int mode); // input_sdl.cpp, also writes controls.ini
// gta2-mp's lobby screen (net/lobby_screen.cpp) takes this screen's inputs and drawing while
// it is up, before the game starts. Weak: the solo binary has no lobby, and these stay null.
extern "C" bool Gta2Net_LobbyActive() __attribute__((weak));
extern "C" void Gta2Net_LobbyInput(int action) __attribute__((weak));
extern "C" void Gta2Net_LobbyDraw(int dw, int dh) __attribute__((weak));

namespace
{
const char* const kSound = "hklm\\software\\dma design ltd\\gta2\\sound";
const char* const kOption = "hklm\\software\\dma design ltd\\gta2\\option";
const char* const kScreen = "hklm\\software\\dma design ltd\\gta2\\screen";

enum Line
{
    kMusic,
    kEffects,
    kTextSpeed,
    kExploding,
    kLighting,
    kFaceButtons,
    kBack,
    kLineCount
};
const char* const kLabels[kLineCount] = {"Music and radio", "Sound effects", "Text speed", "Exploding scores",
                                         "Lighting",        "Face buttons",  "Back"};

bool gOpen = false;
int gLine = 0;
bool gPausedGame = false; // the screen paused the city (and unpauses it on closing)

// Volumes in steps of 10%: 0..10.
int VolumeStep(int vol)
{
    return std::clamp((vol * 10 + 63) / 127, 0, 10);
}
int StepVolume(int step)
{
    return (step * 127 + 5) / 10;
}

int Music()
{
    return gRoot_sound_66B038.GetCDVol_40F120();
}
int Effects()
{
    return gSound_obj_66F680.field_24_sfx_vol;
}
int TextSpeed()
{
    return std::clamp(static_cast<int>(Gta2Native_RegistryDword(kOption, "text_speed", 3)), 1, 5);
}
bool Lighting()
{
    return Gta2Native_RegistryDword(kScreen, "lighting", 1) != 0;
}

void Value(int line, char* out, size_t size)
{
    static const char* const kSpeeds[] = {"", "Fastest", "Fast", "Normal", "Slow", "Slowest"};
    static const char* const kFaces[] = {"Auto", "Nintendo", "Xbox"};
    switch (line)
    {
        case kMusic: snprintf(out, size, "%d%%", VolumeStep(Music()) * 10); break;
        case kEffects: snprintf(out, size, "%d%%", VolumeStep(Effects()) * 10); break;
        case kTextSpeed: snprintf(out, size, "%s", kSpeeds[TextSpeed()]); break;
        case kExploding: snprintf(out, size, "%s", bExplodingScoresOff_67D4FB ? "Off" : "On"); break;
        case kLighting: snprintf(out, size, "%s", Lighting() ? "On" : "Off"); break;
        case kFaceButtons: snprintf(out, size, "%s", kFaces[std::clamp(Gta2Native_FaceButtons(), 0, 2)]); break;
        default: out[0] = 0; break;
    }
}

// Left (-1) or right (+1) on a line.
void Change(int line, int dir)
{
    switch (line)
    {
        case kMusic:
        {
            const int vol = StepVolume(std::clamp(VolumeStep(Music()) + dir, 0, 10));
            gRoot_sound_66B038.SetCDVol_40F0F0(static_cast<u8>(vol));
            gta2_port_reg_set_dword(kSound, "cdvol", vol);
            if (!Gta2Native_IsPlaying())
            {
                // The menu's music takes the volume when its stream opens (sound_obj.cpp,
                // 6/10 of cdvol): the playing one is changed here too.
                gSampManager_6FFF00.StreamSetVolume_58E2F0(static_cast<u8>(6 * vol / 10));
            }
            break;
        }
        case kEffects:
        {
            const int vol = StepVolume(std::clamp(VolumeStep(Effects()) + dir, 0, 10));
            gRoot_sound_66B038.SetSfxVol_40F0B0(static_cast<u8>(vol));
            gta2_port_reg_set_dword(kSound, "sfxvol", vol);
            break;
        }
        case kTextSpeed:
            gta2_port_reg_set_dword(kOption, "text_speed", std::clamp(TextSpeed() + dir, 1, 5));
            break;
        case kExploding:
            gta2_port_reg_set_dword(kScreen, "exploding_on", bExplodingScoresOff_67D4FB ? 1 : 0);
            Init_FrameRateLightAndUnknown_5D8EB0();
            break;
        case kLighting:
            gta2_port_reg_set_dword(kScreen, "lighting", Lighting() ? 0 : 1);
            Init_FrameRateLightAndUnknown_5D8EB0();
            break;
        case kFaceButtons:
            Gta2Native_SetFaceButtons((Gta2Native_FaceButtons() + (dir > 0 ? 1 : 2)) % 3);
            break;
        default:
            return;
    }
    char value[32];
    Value(line, value, sizeof(value));
    fprintf(stderr, "gta2: options: %s %s\n", kLabels[line], value);
}

void Close()
{
    gOpen = false;
    if (gPausedGame && gGame_0x40_67E008 && gGame_0x40_67E008->Is_game_state_Paused_2_416BC0())
    {
        gGame_0x40_67E008->Unpause_4B96C0();
    }
    gPausedGame = false;
    fprintf(stderr, "gta2: options closed\n");
}
} // namespace

extern "C" {

// platform/options_web.cpp's bridge: the front end's Options item (and F8 in the city).
void gta2_js_open_options()
{
    if (!gOpen)
    {
        gOpen = true;
        gLine = 0;
        gPausedGame = false;
        if (Gta2Native_IsPlaying() && gGame_0x40_67E008 && !bStartNetworkGame_7081F0 &&
            gGame_0x40_67E008->field_0_game_state == GameState::Running_1)
        {
            gGame_0x40_67E008->Pause_4B96B0();
            gPausedGame = true;
        }
        fprintf(stderr, "gta2: options opened\n");
    }
}

bool Gta2Native_OptionsOpen()
{
    return gOpen || (Gta2Net_LobbyActive && Gta2Net_LobbyActive());
}

// input_sdl.cpp: 0 up, 1 down, 2 left, 3 right, 4 confirm (A, Return), 5 back (B, Start, Escape).
void Gta2Native_OptionsInput(int action)
{
    if (Gta2Net_LobbyActive && Gta2Net_LobbyActive())
    {
        Gta2Net_LobbyInput(action);
        return;
    }
    if (!gOpen)
    {
        return;
    }
    switch (action)
    {
        case 0: gLine = (gLine + kLineCount - 1) % kLineCount; break;
        case 1: gLine = (gLine + 1) % kLineCount; break;
        case 2: Change(gLine, -1); break;
        case 3: Change(gLine, 1); break;
        case 4:
            if (gLine == kBack)
            {
                Close();
            }
            else
            {
                Change(gLine, 1);
            }
            break;
        case 5: Close(); break;
        default: break;
    }
}

// render/gles_renderer.cpp, after the game's frame: dw x dh drawable pixels.
void Gta2Native_OptionsDraw(int dw, int dh)
{
    if (Gta2Net_LobbyActive && Gta2Net_LobbyActive())
    {
        Gta2Net_LobbyDraw(dw, dh);
        return;
    }
    if (!gOpen)
    {
        return;
    }
    constexpr int kCols = 44; // characters across the widest line (the help line)
    const float px = static_cast<float>(std::max(1, std::min(dh / 200, dw / (kCols * 6 + 8))));
    const float cell = 6 * px;
    const float lineH = 12 * px;
    const uint32_t kWhite = 0xffffffff, kGrey = 0xffa0a0a0, kYellow = 0xff30d0ff, kBar = 0xff303030;
    Gta2Native_OverlayRect(0, 0, static_cast<float>(dw), static_cast<float>(dh), 0xf0000000);

    const float width = kCols * cell;
    const float x0 = (dw - width) / 2;
    const float height = (kLineCount + 4) * lineH;
    float y = (dh - height) / 2;
    Gta2Native_OverlayText(x0, y, px, kWhite, "Options");
    y += 2 * lineH;
    for (int i = 0; i < kLineCount; ++i, y += lineH)
    {
        const bool sel = i == gLine;
        if (sel)
        {
            Gta2Native_OverlayRect(x0 - cell, y - 3 * px, width + 2 * cell, lineH, kBar);
        }
        const uint32_t colour = sel ? kYellow : kWhite;
        Gta2Native_OverlayText(x0, y, px, colour, kLabels[i]);
        char value[32];
        Value(i, value, sizeof(value));
        if (value[0])
        {
            char shown[40];
            snprintf(shown, sizeof(shown), sel ? "< %s >" : "%s", value);
            const float vx = x0 + width - static_cast<float>(strlen(shown)) * cell;
            Gta2Native_OverlayText(vx, y, px, colour, shown);
        }
    }
    y += lineH;
    Gta2Native_OverlayText(x0, y, px, kGrey, "Up/down: choose   Left/right: change");
    Gta2Native_OverlayText(x0, y + lineH, px, kGrey, "B: back   Text speed: from the next game");
}

} // extern "C"
#endif
