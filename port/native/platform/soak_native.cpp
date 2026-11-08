// Randomised input driver for headless soak tests (jalon 2), the native counterpart of
// the web build's soaks (docs/browser-build-status.md): walking, driving, shooting, car
// entries. It presses the original's default keys (patch 0008 restores them), through
// the same entry point as the keyboard (gta2_port_key, port/platform/dinput_web.cpp).
//
// GTA2_SOAK=1 turns it on (main.cpp); GTA2_SOAK_SEED picks the sequence (default 1), so a
// failing run can be replayed. The test aids used (gta2_dev_give_weapon, gta2_dev_spawn_car)
// are the web developer probe's, and use the game's own functions.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

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
};
// The original's defaults (gDefaultControls_61A9E4): arrows, LCtrl, Return, Space, Z, X.
const Key kForward{0xC8, 0x26};
const Key kBack{0xD0, 0x28};
const Key kLeft{0xCB, 0x25};
const Key kRight{0xCD, 0x27};
const Key kAttack{0x1D, 0x11};
const Key kEnter{0x1C, 0x0D};
const Key kJump{0x39, 0x20};
const Key kPrevWeapon{0x2C, 0x5A};
const Key kNextWeapon{0x2D, 0x58};

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
    gta2_port_key(k.scancode, k.vk, 1, 0);
    gHeld.push_back({k, now + ms});
}

void ReleaseDue(double now)
{
    for (size_t i = 0; i < gHeld.size();)
    {
        if (now >= gHeld[i].until)
        {
            gta2_port_key(gHeld[i].key.scancode, gHeld[i].key.vk, 0, 0);
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

extern "C" void Gta2Native_SoakInit()
{
    const char* seed = getenv("GTA2_SOAK_SEED");
    gRng = seed ? static_cast<uint32_t>(strtoul(seed, nullptr, 10)) : 1;
    if (gRng == 0)
    {
        gRng = 1;
    }
    fprintf(stderr, "gta2: soak driver on (seed %u)\n", gRng);
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
            gta2_port_release_all_keys();
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
