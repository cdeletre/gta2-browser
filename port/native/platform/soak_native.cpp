// Randomised input driver for headless soak tests (jalon 2), the native counterpart of
// the web build's soaks (docs/browser-build-status.md): walking, driving, shooting, car
// entries. It presses the original's default keys (patch 0008 restores them), through
// the same entry point as the keyboard (gta2_port_key, port/platform/dinput_web.cpp).
//
// GTA2_SOAK=1 turns it on (main.cpp); GTA2_SOAK_SEED picks the sequence (default 1), so a
// failing run can be replayed. GTA2_SOAK_INPUT=keyboard or pad (windowed builds) sends the
// presses as SDL keyboard or controller events instead, through input_sdl.cpp, which
// tests the native input mapping end to end. The test aids used (gta2_dev_give_weapon, gta2_dev_spawn_car)
// are the web developer probe's, and use the game's own functions.
// GTA2_SOAK_AIDS=0 leaves them out (keys only): they change this player's game alone, so a
// co-op match (gta2-mp, lockstep) would drift apart at once.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#if !defined(GTA2_NATIVE_HEADLESS)
#include <SDL.h>
extern "C" int Gta2Native_PadButtonForLabel(int label); // input_sdl.cpp
#endif

extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void gta2_port_release_all_keys();
extern "C" const int* gta2_dev_state();
extern "C" int gta2_dev_give_weapon(int kind, int ammo);
extern "C" int gta2_dev_spawn_car(int model, int dx, int dy, int rot);

namespace
{
struct Key
{
    int scancode; // DirectInput
    int vk;
    int sdlScancode; // the same key for GTA2_SOAK_INPUT=keyboard (SDL_Scancode)
    int padButton;   // the controller button for it in the city (SDL_GameControllerButton),
                     // or kTriggerL2 / kTriggerR2
};
constexpr int kTriggerL2 = 100; // the triggers are axes in SDL
constexpr int kTriggerR2 = 101;
// The original's defaults (gDefaultControls_61A9E4): arrows, LCtrl, Return, Space, Z, X;
// controller buttons as input_sdl.cpp maps them (the PlayStation-like layout: R2 attacks,
// R1 jumps, L1 / L2 next / previous weapon, Y gets in). SDL values: SDL_scancode.h and
// SDL_gamecontroller.h (UP 82, DOWN 81, LEFT 80, RIGHT 79, LCTRL 224, RETURN 40, SPACE 44,
// Z 29, X 27; A 0, X 2, Y 3, LEFTSHOULDER 9, RIGHTSHOULDER 10, DPAD_UP 11..DPAD_RIGHT 14).
const Key kForward{0xC8, 0x26, 82, 11};
const Key kBack{0xD0, 0x28, 81, 12};
const Key kLeft{0xCB, 0x25, 80, 13};
const Key kRight{0xCD, 0x27, 79, 14};
const Key kAttack{0x1D, 0x11, 224, kTriggerR2};
const Key kEnter{0x1C, 0x0D, 40, 3};
const Key kJump{0x39, 0x20, 44, 10};
const Key kPrevWeapon{0x2C, 0x5A, 29, kTriggerL2};
const Key kNextWeapon{0x2D, 0x58, 27, 9};
// Menus: A confirms, B goes back, the d-pad moves (input_sdl.cpp's menu layout).
const Key kMenuOk{0x1C, 0x0D, 40, 0};
const Key kMenuBack{0x01, 0x1B, 41, 1};
// Start (Escape: the pause) and Select (F8 on a keyboard: the Options screen in the city).
const Key kStartButton{0x01, 0x1B, 41, 6};
const Key kSelectButton{0x42, 0x77, 65, 4};

enum InputMode
{
    kDirect,
    kKeyboard,
    kPad
};
InputMode gMode = kDirect;

void Send(const Key& k, bool down)
{
#if !defined(GTA2_NATIVE_HEADLESS)
    if (gMode == kKeyboard)
    {
        SDL_Event e{};
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.scancode = static_cast<SDL_Scancode>(k.sdlScancode);
        e.key.keysym.sym = SDL_GetKeyFromScancode(e.key.keysym.scancode);
        SDL_PushEvent(&e);
        return;
    }
    if (gMode == kPad)
    {
        // padButton holds the label (A 0, B 1, X 2, Y 3); input_sdl.cpp knows which button
        // carries it (GTA2_FACE_BUTTONS).
        SDL_Event e{};
        if (k.padButton == kTriggerL2 || k.padButton == kTriggerR2)
        {
            e.type = SDL_CONTROLLERAXISMOTION;
            e.caxis.axis = k.padButton == kTriggerL2 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            e.caxis.value = down ? 32767 : 0;
            SDL_PushEvent(&e);
            return;
        }
        e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
        e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.cbutton.button = static_cast<Uint8>(Gta2Native_PadButtonForLabel(k.padButton));
        SDL_PushEvent(&e);
        return;
    }
#endif
    gta2_port_key(k.scancode, k.vk, down ? 1 : 0, 0);
}

uint32_t gRng = 1;
uint32_t Next()
{
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}
int Range(int lo, int hi) // inclusive
{
    return lo + static_cast<int>(Next() % static_cast<uint32_t>(hi - lo + 1));
}

struct Held
{
    Key key;
    double until;
};
std::vector<Held> gHeld;
double gNextAction = 0;
double gNextCar = 0;
bool gArmed = false;
unsigned gActions = 0;
unsigned gCarEntries = 0;

void Press(const Key& k, double now, int ms)
{
    for (const Held& h : gHeld)
    {
        if (h.key.scancode == k.scancode)
        {
            return;
        }
    }
    Send(k, true);
    gHeld.push_back({k, now + ms});
}

void ReleaseDue(double now)
{
    for (size_t i = 0; i < gHeld.size();)
    {
        if (now >= gHeld[i].until)
        {
            Send(gHeld[i].key, false);
            gHeld[i] = gHeld.back();
            gHeld.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

void Act(double now)
{
    ++gActions;
    const int roll = Range(0, 99);
    if (roll < 45) // move: forward, often while turning
    {
        const int ms = Range(400, 3000);
        Press(kForward, now, ms);
        if (Range(0, 2) != 0)
        {
            Press(Range(0, 1) ? kLeft : kRight, now + Range(0, ms / 2), Range(150, 900));
        }
    }
    else if (roll < 55)
    {
        Press(kBack, now, Range(300, 1200));
    }
    else if (roll < 70)
    {
        Press(kAttack, now, Range(200, 1500));
    }
    else if (roll < 78)
    {
        Press(kEnter, now, 120); // get in or out of a car
    }
    else if (roll < 86)
    {
        Press(kJump, now, Range(100, 600)); // jump, or handbrake in a car
    }
    else if (roll < 93)
    {
        Press(Range(0, 1) ? kPrevWeapon : kNextWeapon, now, 120);
    }
    else
    {
        Press(Range(0, 1) ? kLeft : kRight, now, Range(200, 1200));
    }
    gNextAction = now + Range(200, 1500);
}
} // namespace

// The input mode (GTA2_SOAK_INPUT) also applies to the autostart's presses (main.cpp).
extern "C" void Gta2Native_InputModeInit()
{
    if (const char* mode = getenv("GTA2_SOAK_INPUT"))
    {
        gMode = !strcmp(mode, "keyboard") ? kKeyboard : !strcmp(mode, "pad") ? kPad : kDirect;
    }
}

// The autostart's Return in the menus (A on the controller).
extern "C" void Gta2Native_AutostartKey(int which, bool down)
{
    // main.cpp's menu key names: ok, up, down, left, right, back, start, select.
    static const Key* const kKeys[] = {&kMenuOk, &kForward, &kBack, &kLeft, &kRight, &kMenuBack, &kStartButton, &kSelectButton};
    if (which >= 0 && which < 8)
    {
        Send(*kKeys[which], down);
    }
}

extern "C" void Gta2Native_SoakInit()
{
    const char* seed = getenv("GTA2_SOAK_SEED");
    gRng = seed ? static_cast<uint32_t>(strtoul(seed, nullptr, 10)) : 1;
    if (gRng == 0)
    {
        gRng = 1;
    }
    fprintf(stderr, "gta2: soak driver on (seed %u, input %s)\n", gRng,
            gMode == kKeyboard ? "SDL keyboard events" : gMode == kPad ? "SDL controller events" : "direct");
}

// Called once per loop iteration with the run's clock (ms).
extern "C" void Gta2Native_SoakStep(double now)
{
    ReleaseDue(now);
    const int* s = gta2_dev_state();
    if (!s[0])
    {
        return; // no player yet (front end, loading)
    }
    static const bool aids = !getenv("GTA2_SOAK_AIDS") || atoi(getenv("GTA2_SOAK_AIDS")) != 0;
    if (!aids)
    {
        gArmed = true;
        gNextCar = 1e300; // never
    }
    if (!gArmed)
    {
        gArmed = gta2_dev_give_weapon(0, 500) != 0;
        gNextCar = now + 5000;
    }
    if (now >= gNextCar)
    {
        gNextCar = now + 30000;
        if (!s[1] && gta2_dev_spawn_car(10, 1, 0, 0))
        {
            ++gCarEntries;
            for (const Held& h : gHeld)
            {
                Send(h.key, false);
            }
            gHeld.clear();
            Press(kEnter, now, 120); // walks to the door and gets in (about 4.5 s)
            gNextAction = now + 6000;
            return;
        }
    }
    if (now >= gNextAction)
    {
        Act(now);
    }
}

extern "C" void Gta2Native_SoakSummary(char* out, int size)
{
    snprintf(out, size, "soak: %u actions, %u cars spawned for entry", gActions, gCarEntries);
}
