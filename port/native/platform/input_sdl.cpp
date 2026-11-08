// Keyboard and game controller input for the native build (jalon 4).
//
// Both end up as the same key presses as in the browser: gta2_port_key (DirectInput scan
// code, Win32 virtual key, typed character), port/platform/dinput_web.cpp. The game reads
// its own controls; the native build keeps the original's default bindings (patch 0008):
// arrows, Left Ctrl (attack), Return (enter/exit car), Space (jump/handbrake), Z/X
// (weapons), Tab and Left Alt (special 1/2), Right Shift (status).
//
// Controller (SDL_GameController; buttons by position, A = bottom, B = right, X = left,
// Y = top), as web/gamepad.js but on the original's keys:
//   in the city:  d-pad / left stick = arrows, R2 = up (accelerate), L2 = down (brake),
//                 A = attack, Y = enter/exit car, X = jump/handbrake, B = special 1,
//                 L1/R1 = previous/next weapon, L3 = special 2, R3 = status,
//                 Start = Escape (the game's pause / quit prompt)
//   in the menus: d-pad / left stick = arrows, A = Return, B and Start = Escape
// Buttons held when the context changes are ignored until released, so the A that
// confirms a menu does not also fire a shot. Select (Back) + Start quits the game.
// SDL reads extra mappings from SDL_GAMECONTROLLERCONFIG (set by PortMaster), and from
// gamecontrollerdb.txt in the game folder if there is one.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <SDL.h>
#include <stdio.h>

#include <set>

extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void gta2_port_release_all_keys();
extern "C" bool Gta2Native_IsPlaying(); // status_native.cpp

namespace
{
struct Key
{
    int dik; // DirectInput scan code
    int vk;  // Win32 virtual key
    bool operator<(const Key& o) const
    {
        return dik < o.dik;
    }
};

// SDL scan code -> DirectInput scan code and virtual key (web/shell.js KEYS).
struct KeyRow
{
    SDL_Scancode sdl;
    Key key;
};
const KeyRow kKeyboard[] = {
    {SDL_SCANCODE_ESCAPE, {0x01, 0x1b}},     {SDL_SCANCODE_1, {0x02, 0x31}},
    {SDL_SCANCODE_2, {0x03, 0x32}},          {SDL_SCANCODE_3, {0x04, 0x33}},
    {SDL_SCANCODE_4, {0x05, 0x34}},          {SDL_SCANCODE_5, {0x06, 0x35}},
    {SDL_SCANCODE_6, {0x07, 0x36}},          {SDL_SCANCODE_7, {0x08, 0x37}},
    {SDL_SCANCODE_8, {0x09, 0x38}},          {SDL_SCANCODE_9, {0x0a, 0x39}},
    {SDL_SCANCODE_0, {0x0b, 0x30}},          {SDL_SCANCODE_MINUS, {0x0c, 0xbd}},
    {SDL_SCANCODE_EQUALS, {0x0d, 0xbb}},     {SDL_SCANCODE_BACKSPACE, {0x0e, 0x08}},
    {SDL_SCANCODE_TAB, {0x0f, 0x09}},        {SDL_SCANCODE_Q, {0x10, 0x51}},
    {SDL_SCANCODE_W, {0x11, 0x57}},          {SDL_SCANCODE_E, {0x12, 0x45}},
    {SDL_SCANCODE_R, {0x13, 0x52}},          {SDL_SCANCODE_T, {0x14, 0x54}},
    {SDL_SCANCODE_Y, {0x15, 0x59}},          {SDL_SCANCODE_U, {0x16, 0x55}},
    {SDL_SCANCODE_I, {0x17, 0x49}},          {SDL_SCANCODE_O, {0x18, 0x4f}},
    {SDL_SCANCODE_P, {0x19, 0x50}},          {SDL_SCANCODE_LEFTBRACKET, {0x1a, 0xdb}},
    {SDL_SCANCODE_RIGHTBRACKET, {0x1b, 0xdd}}, {SDL_SCANCODE_RETURN, {0x1c, 0x0d}},
    {SDL_SCANCODE_LCTRL, {0x1d, 0x11}},      {SDL_SCANCODE_A, {0x1e, 0x41}},
    {SDL_SCANCODE_S, {0x1f, 0x53}},          {SDL_SCANCODE_D, {0x20, 0x44}},
    {SDL_SCANCODE_F, {0x21, 0x46}},          {SDL_SCANCODE_G, {0x22, 0x47}},
    {SDL_SCANCODE_H, {0x23, 0x48}},          {SDL_SCANCODE_J, {0x24, 0x4a}},
    {SDL_SCANCODE_K, {0x25, 0x4b}},          {SDL_SCANCODE_L, {0x26, 0x4c}},
    {SDL_SCANCODE_SEMICOLON, {0x27, 0xba}},  {SDL_SCANCODE_APOSTROPHE, {0x28, 0xde}},
    {SDL_SCANCODE_GRAVE, {0x29, 0xc0}},      {SDL_SCANCODE_LSHIFT, {0x2a, 0x10}},
    {SDL_SCANCODE_BACKSLASH, {0x2b, 0xdc}},  {SDL_SCANCODE_Z, {0x2c, 0x5a}},
    {SDL_SCANCODE_X, {0x2d, 0x58}},          {SDL_SCANCODE_C, {0x2e, 0x43}},
    {SDL_SCANCODE_V, {0x2f, 0x56}},          {SDL_SCANCODE_B, {0x30, 0x42}},
    {SDL_SCANCODE_N, {0x31, 0x4e}},          {SDL_SCANCODE_M, {0x32, 0x4d}},
    {SDL_SCANCODE_COMMA, {0x33, 0xbc}},      {SDL_SCANCODE_PERIOD, {0x34, 0xbe}},
    {SDL_SCANCODE_SLASH, {0x35, 0xbf}},      {SDL_SCANCODE_RSHIFT, {0x36, 0x10}},
    {SDL_SCANCODE_KP_MULTIPLY, {0x37, 0x6a}}, {SDL_SCANCODE_LALT, {0x38, 0x12}},
    {SDL_SCANCODE_SPACE, {0x39, 0x20}},      {SDL_SCANCODE_CAPSLOCK, {0x3a, 0x14}},
    {SDL_SCANCODE_F1, {0x3b, 0x70}},         {SDL_SCANCODE_F2, {0x3c, 0x71}},
    {SDL_SCANCODE_F3, {0x3d, 0x72}},         {SDL_SCANCODE_F4, {0x3e, 0x73}},
    {SDL_SCANCODE_F5, {0x3f, 0x74}},         {SDL_SCANCODE_F6, {0x40, 0x75}},
    {SDL_SCANCODE_F7, {0x41, 0x76}},         {SDL_SCANCODE_F8, {0x42, 0x77}},
    {SDL_SCANCODE_F9, {0x43, 0x78}},         {SDL_SCANCODE_F10, {0x44, 0x79}},
    {SDL_SCANCODE_NUMLOCKCLEAR, {0x45, 0x90}}, {SDL_SCANCODE_SCROLLLOCK, {0x46, 0x91}},
    {SDL_SCANCODE_KP_7, {0x47, 0x67}},       {SDL_SCANCODE_KP_8, {0x48, 0x68}},
    {SDL_SCANCODE_KP_9, {0x49, 0x69}},       {SDL_SCANCODE_KP_MINUS, {0x4a, 0x6d}},
    {SDL_SCANCODE_KP_4, {0x4b, 0x64}},       {SDL_SCANCODE_KP_5, {0x4c, 0x65}},
    {SDL_SCANCODE_KP_6, {0x4d, 0x66}},       {SDL_SCANCODE_KP_PLUS, {0x4e, 0x6b}},
    {SDL_SCANCODE_KP_1, {0x4f, 0x61}},       {SDL_SCANCODE_KP_2, {0x50, 0x62}},
    {SDL_SCANCODE_KP_3, {0x51, 0x63}},       {SDL_SCANCODE_KP_0, {0x52, 0x60}},
    {SDL_SCANCODE_KP_PERIOD, {0x53, 0x6e}},  {SDL_SCANCODE_F11, {0x57, 0x7a}},
    {SDL_SCANCODE_F12, {0x58, 0x7b}},        {SDL_SCANCODE_KP_ENTER, {0x9c, 0x0d}},
    {SDL_SCANCODE_RCTRL, {0x9d, 0x11}},      {SDL_SCANCODE_KP_DIVIDE, {0xb5, 0x6f}},
    {SDL_SCANCODE_RALT, {0xb8, 0x12}},       {SDL_SCANCODE_HOME, {0xc7, 0x24}},
    {SDL_SCANCODE_UP, {0xc8, 0x26}},         {SDL_SCANCODE_PAGEUP, {0xc9, 0x21}},
    {SDL_SCANCODE_LEFT, {0xcb, 0x25}},       {SDL_SCANCODE_RIGHT, {0xcd, 0x27}},
    {SDL_SCANCODE_END, {0xcf, 0x23}},        {SDL_SCANCODE_DOWN, {0xd0, 0x28}},
    {SDL_SCANCODE_PAGEDOWN, {0xd1, 0x22}},   {SDL_SCANCODE_INSERT, {0xd2, 0x2d}},
    {SDL_SCANCODE_DELETE, {0xd3, 0x2e}},
};

const Key kUp{0xc8, 0x26}, kDown{0xd0, 0x28}, kLeft{0xcb, 0x25}, kRight{0xcd, 0x27};
const Key kReturn{0x1c, 0x0d}, kEscape{0x01, 0x1b}, kLCtrl{0x1d, 0x11}, kSpace{0x39, 0x20};
const Key kZ{0x2c, 0x5a}, kX{0x2d, 0x58}, kTab{0x0f, 0x09}, kLAlt{0x38, 0x12}, kRShift{0x36, 0x10};

const Key* KeyFor(SDL_Scancode sc)
{
    for (const KeyRow& row : kKeyboard)
    {
        if (row.sdl == sc)
        {
            return &row.key;
        }
    }
    return nullptr;
}

// The character typed with a key, as web/shell.js (KeyboardEvent.key of one character).
int TypedChar(const SDL_Keysym& k)
{
    const SDL_Keycode sym = k.sym;
    if (sym < 32 || sym > 126)
    {
        return 0;
    }
    if (sym >= 'a' && sym <= 'z' && (k.mod & (KMOD_SHIFT | KMOD_CAPS)))
    {
        return sym - 'a' + 'A';
    }
    return sym;
}

// --- controller -------------------------------------------------------------------
enum Button
{
    kA,
    kB,
    kXBtn,
    kY,
    kBack,
    kStart,
    kL1,
    kR1,
    kL3,
    kR3,
    kPadUp,
    kPadDown,
    kPadLeft,
    kPadRight,
    kL2,
    kR2,
    kStickUp,
    kStickDown,
    kStickLeft,
    kStickRight,
    kButtonCount
};
constexpr int kStickDeadzone = static_cast<int>(0.35 * 32767); // web/gamepad.js
constexpr int kTriggerThreshold = 16384;

bool gPressed[kButtonCount] = {};
bool gBlocked[kButtonCount] = {};
int gPlaying = -1; // unknown
std::set<Key> gHeld; // keys the controller is holding down
int gControllers = 0;

int ButtonFromSdl(Uint8 b)
{
    switch (b)
    {
        case SDL_CONTROLLER_BUTTON_A: return kA;
        case SDL_CONTROLLER_BUTTON_B: return kB;
        case SDL_CONTROLLER_BUTTON_X: return kXBtn;
        case SDL_CONTROLLER_BUTTON_Y: return kY;
        case SDL_CONTROLLER_BUTTON_BACK: return kBack;
        case SDL_CONTROLLER_BUTTON_START: return kStart;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kL1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kR1;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kL3;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kR3;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return kPadUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kPadDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kPadLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kPadRight;
        default: return -1;
    }
}

void Add(std::set<Key>& keys, int button, bool playing)
{
    switch (button)
    {
        case kPadUp:
        case kStickUp: keys.insert(kUp); break;
        case kPadDown:
        case kStickDown: keys.insert(kDown); break;
        case kPadLeft:
        case kStickLeft: keys.insert(kLeft); break;
        case kPadRight:
        case kStickRight: keys.insert(kRight); break;
        case kStart: keys.insert(kEscape); break;
        default: break;
    }
    if (playing)
    {
        switch (button)
        {
            case kR2: keys.insert(kUp); break;
            case kL2: keys.insert(kDown); break;
            case kA: keys.insert(kLCtrl); break;
            case kY: keys.insert(kReturn); break;
            case kXBtn: keys.insert(kSpace); break;
            case kB: keys.insert(kTab); break;
            case kL1: keys.insert(kZ); break;
            case kR1: keys.insert(kX); break;
            case kL3: keys.insert(kLAlt); break;
            case kR3: keys.insert(kRShift); break;
            default: break;
        }
    }
    else
    {
        switch (button)
        {
            case kA: keys.insert(kReturn); break;
            case kB: keys.insert(kEscape); break;
            default: break;
        }
    }
}

void Apply(const std::set<Key>& keys)
{
    for (const Key& k : gHeld)
    {
        if (!keys.count(k))
        {
            gta2_port_key(k.dik, k.vk, 0, 0);
        }
    }
    for (const Key& k : keys)
    {
        if (!gHeld.count(k))
        {
            gta2_port_key(k.dik, k.vk, 1, 0);
        }
    }
    gHeld = keys;
}

void ReleaseAll()
{
    Apply({});
    for (bool& p : gPressed)
    {
        p = false;
    }
    gta2_port_release_all_keys();
}

void OpenController(int index)
{
    if (!SDL_IsGameController(index))
    {
        fprintf(stderr, "gta2: joystick %d (%s) has no controller mapping\n", index, SDL_JoystickNameForIndex(index));
        return;
    }
    if (SDL_GameController* c = SDL_GameControllerOpen(index))
    {
        ++gControllers;
        fprintf(stderr, "gta2: controller \"%s\"\n", SDL_GameControllerName(c));
    }
}
} // namespace

extern "C" {

void Gta2Native_InputInit(const char* gameDir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/gamecontrollerdb.txt", gameDir);
    const int added = SDL_GameControllerAddMappingsFromFile(path);
    if (added > 0)
    {
        fprintf(stderr, "gta2: %d controller mappings from %s\n", added, path);
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        OpenController(i);
    }
}

// Returns false when the player asked to quit (Select + Start).
bool Gta2Native_InputEvent(const SDL_Event& e)
{
    switch (e.type)
    {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (const Key* k = KeyFor(e.key.keysym.scancode))
            {
                const bool down = e.type == SDL_KEYDOWN;
                if (down && e.key.repeat)
                {
                    break; // as web/shell.js: the game sees one press
                }
                gta2_port_key(k->dik, k->vk, down ? 1 : 0, down ? TypedChar(e.key.keysym) : 0);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            OpenController(e.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            if (const int b = ButtonFromSdl(e.cbutton.button); b >= 0)
            {
                gPressed[b] = e.type == SDL_CONTROLLERBUTTONDOWN;
            }
            if (gPressed[kBack] && gPressed[kStart])
            {
                fprintf(stderr, "gta2: Select + Start: quitting\n");
                return false;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
        {
            const int v = e.caxis.value;
            switch (e.caxis.axis)
            {
                case SDL_CONTROLLER_AXIS_LEFTX:
                    gPressed[kStickLeft] = v < -kStickDeadzone;
                    gPressed[kStickRight] = v > kStickDeadzone;
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    gPressed[kStickUp] = v < -kStickDeadzone;
                    gPressed[kStickDown] = v > kStickDeadzone;
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT: gPressed[kL2] = v > kTriggerThreshold; break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: gPressed[kR2] = v > kTriggerThreshold; break;
                default: break;
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                ReleaseAll();
            }
            break;
        default:
            break;
    }
    return true;
}

// Once per loop iteration, after the events: turns the controller's state into keys.
void Gta2Native_InputFrame()
{
    const int playing = Gta2Native_IsPlaying() ? 1 : 0;
    if (playing != gPlaying)
    {
        if (gPlaying != -1)
        {
            for (int b = 0; b < kButtonCount; ++b)
            {
                gBlocked[b] = gPressed[b];
            }
        }
        gPlaying = playing;
    }
    std::set<Key> keys;
    for (int b = 0; b < kButtonCount; ++b)
    {
        if (!gPressed[b])
        {
            gBlocked[b] = false;
        }
        // Select is the quit modifier: Start does nothing while it is held.
        if (gPressed[b] && !gBlocked[b] && !(b == kStart && gPressed[kBack]))
        {
            Add(keys, b, playing != 0);
        }
    }
    Apply(keys);
}

} // extern "C"
#endif
