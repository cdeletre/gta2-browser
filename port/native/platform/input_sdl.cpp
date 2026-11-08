// Keyboard and game controller input for the native build (jalon 4).
//
// Both end up as the same key presses as in the browser: gta2_port_key (DirectInput scan
// code, Win32 virtual key, typed character), port/platform/dinput_web.cpp.
//
// Keyboard: SDL key events, web/shell.js's key table. The game reads its own controls; the
// native build keeps the original's default bindings (patch 0008): arrows, Left Ctrl
// (attack), Return (enter/exit car), Space (jump/handbrake), Z/X (weapons), Tab and Left
// Alt (special 1/2), Right Shift (status), F1/F2 (radio station).
//
// Controller (SDL_GameController): conf/controls.ini, written with the defaults and
// comments on the first run (kDefaultControls below). As web/gamepad.js, a "city" and a
// "menu" layout; buttons held when the context changes are ignored until released, so
// the A that confirms a menu does not also fire a shot. City actions are the game's own
// controls (the 12 slots of hklm\...\gta2\control, read from the registry on entering the
// city), so a key rebound in the game's settings moves with its button. The labels a, b,
// x, y follow face_buttons. SDL's own mappings name positions (its A is the bottom
// button), while handhelds are usually labelled Nintendo-style (A right, B bottom). The
// mappings PortMaster exports in SDL_GAMECONTROLLERCONFIG name the labels instead (R36S:
// its "a" is the button printed A), so face_buttons = auto (the default) takes SDL's names
// as they are when that variable is set, and swaps A/B and X/Y otherwise.
// GTA2_FACE_BUTTONS (auto, nintendo, xbox) overrides the file.
// While the game's quit prompt is up, the menu layout applies: A (confirm) quits, twice
// because of the port's "ARE YOU SURE?", B or Start (back) carries on. The launcher can
// also end the game (gptokeyb kills it, which SDL turns into SDL_QUIT).
// SDL also reads mappings from SDL_GAMECONTROLLERCONFIG (PortMaster) and from
// conf/gamecontrollerdb.txt if there is one.
// GTA2_LOG_INPUT=1 logs the controller's SDL mapping, every button and the keys sent to
// the game, with the layout in use (city or menu).
#if !defined(GTA2_NATIVE_HEADLESS)
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <string>

// port_prefix.h (forced into every file) routes fopen/fclose to the game's versions, which
// lowercase and resolve game paths; controls.ini has its own absolute path.
#undef fopen
#undef fclose

extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void gta2_port_release_all_keys();
extern "C" bool Gta2Native_IsPlaying();       // status_native.cpp
extern "C" bool Gta2Native_QuitPromptShown(); // status_native.cpp
extern "C" unsigned int Gta2Native_RegistryDword(const char* path, const char* name, unsigned int fallback);

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
const Key kUp{0xc8, 0x26}, kDown{0xd0, 0x28}, kLeft{0xcb, 0x25}, kRight{0xcd, 0x27};
const Key kReturn{0x1c, 0x0d}, kEscape{0x01, 0x1b};
// The original's radio keys (Player::Hud_Controls_565890): F1 next station, F2 previous.
const Key kF1{0x3b, 0x70}, kF2{0x3c, 0x71};

// The game's 12 control slots (hklm\...\gta2\control\0..11) and the original's keys for
// them (gDefaultControls_61A9E4), used when the registry has no value.
const char* const kSlotNames[12] = {"forward", "backward", "left",        "right",    "attack",   "enter_exit",
                                    "jump",    "prev_weapon", "next_weapon", "special1", "special2", "status"};
const int kDefaultSlotKeys[12] = {0xC8, 0xD0, 0xCB, 0xCD, 0x1D, 0x1C, 0x39, 0x2C, 0x2D, 0x0F, 0x38, 0x36};
const char* const kControlKey = "hklm\\software\\dma design ltd\\gta2\\control";

enum Button
{
    kA, kB, kXBtn, kY, kSelect, kStart, kL1, kR1, kL3, kR3, kPadUp, kPadDown, kPadLeft, kPadRight, kL2, kR2,
    kButtonCount,
    // the left stick, as the d-pad (left_stick = dpad)
    kStickUp = kButtonCount, kStickDown, kStickLeft, kStickRight,
    kInputCount
};
const char* const kButtonNames[kButtonCount] = {"a",  "b",  "x",  "y",  "select", "start", "l1",    "r1",
                                                "l3", "r3", "up", "down", "left",   "right", "l2", "r2"};

enum ActionKind
{
    kNone,
    kSlot, // one of the game's 12 controls (city)
    kFixed // a fixed key: pause, menu actions, key:<name>
};
struct Action
{
    ActionKind kind = kNone;
    int slot = 0;
    Key key{0, 0};
};
enum Layout
{
    kCity,
    kMenu,
    kLayoutCount
};
Action gBindings[kLayoutCount][kButtonCount];
Key gSlotKeys[12]; // current key of each slot (registry), refreshed on entering the city

bool gNintendoLabels = true;
int gFaceMode = 0; // face_buttons: 0 auto, 1 nintendo, 2 xbox
bool gLogInput = false; // GTA2_LOG_INPUT
bool gStickAsDpad = true;
int gStickDeadzone = static_cast<int>(0.35 * 32767); // web/gamepad.js
constexpr int kTriggerThreshold = 16384;
std::string gConfDir = ".";

bool gPressed[kInputCount] = {};
bool gBlocked[kInputCount] = {};
int gPlaying = -1; // unknown
std::set<Key> gHeld; // keys the controller is holding down

// Written to conf/controls.ini when it is missing. Every line is optional.
const char* const kDefaultControls = R"(# GTA2 controller layout
#
# Buttons: a b x y l1 r1 l2 r2 l3 r3 up down left right start select
#   a, b, x, y are the labels printed on the buttons (see face_buttons).
#   up, down, left, right are the d-pad (and the left stick, see left_stick).
# One action per button and per section; several buttons may share an action.
# "none" leaves a button unused. A missing line keeps the default shown here.
#
# [city] actions (on foot or driving): the game's own controls
#   forward backward left right attack enter_exit jump prev_weapon next_weapon
#   special1 special2 status
#   and: pause (Escape: the game's pause and quit prompt),
#        radio_next, radio_prev (car radio station, F1 and F2 in the original)
# [menu] actions (front end menus): up down left right confirm back
# Both sections also accept key:<name>, a keyboard key by its SDL name
#   (key:Escape, key:Return, key:F1, key:Tab ...).
#
# Quitting: Start (pause) opens the quit prompt, then A twice quits, B carries on.
# The PortMaster launcher can also end the game.

[options]
# Which physical button is "a", "b", "x" and "y" below:
# auto:     the names of the launcher's controller mapping (PortMaster), else nintendo
# nintendo: A right, B bottom, X top, Y left (most handhelds)
# xbox:     A bottom, B right, X left, Y top
# If A and B (or X and Y) come out swapped, try nintendo or xbox.
face_buttons = auto
# dpad: the left stick moves like the d-pad; off: the left stick is ignored
left_stick = dpad
# how far the stick must move, 0.05 to 0.95
stick_deadzone = 0.35

[city]
up = forward
down = backward
left = left
right = right
r2 = forward
l2 = backward
a = attack
y = enter_exit
x = jump
b = special1
l1 = prev_weapon
r1 = next_weapon
l3 = special2
r3 = status
start = pause
select = radio_next

[menu]
up = up
down = down
left = left
right = right
a = confirm
b = back
start = back
select = none
x = none
y = none
)";

std::string Trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a])))
    {
        ++a;
    }
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1])))
    {
        --b;
    }
    return s.substr(a, b - a);
}

std::string LowerStr(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool ParseAction(Layout layout, const std::string& value, Action* out)
{
    const std::string v = LowerStr(value);
    if (v == "none")
    {
        *out = Action();
        return true;
    }
    if (v.compare(0, 4, "key:") == 0)
    {
        const SDL_Scancode sc = SDL_GetScancodeFromName(value.substr(4).c_str());
        const Key* k = sc != SDL_SCANCODE_UNKNOWN ? KeyFor(sc) : nullptr;
        if (!k)
        {
            return false;
        }
        out->kind = kFixed;
        out->key = *k;
        return true;
    }
    if (layout == kCity)
    {
        for (int s = 0; s < 12; ++s)
        {
            if (v == kSlotNames[s])
            {
                out->kind = kSlot;
                out->slot = s;
                return true;
            }
        }
        const struct
        {
            const char* name;
            Key key;
        } kCityActions[] = {{"pause", kEscape}, {"radio_next", kF1}, {"radio_prev", kF2}};
        for (const auto& c : kCityActions)
        {
            if (v == c.name)
            {
                out->kind = kFixed;
                out->key = c.key;
                return true;
            }
        }
        return false;
    }
    const struct
    {
        const char* name;
        Key key;
    } kMenuActions[] = {{"up", kUp}, {"down", kDown}, {"left", kLeft}, {"right", kRight}, {"confirm", kReturn}, {"back", kEscape}};
    for (const auto& m : kMenuActions)
    {
        if (v == m.name)
        {
            out->kind = kFixed;
            out->key = m.key;
            return true;
        }
    }
    return false;
}

// Reads one controls.ini text (the defaults first, then the user's file over them).
void ParseControls(const std::string& text, const char* source)
{
    enum Section
    {
        kOther,
        kOptions,
        kCitySection,
        kMenuSection
    } section = kOther;
    int lineNo = 0;
    for (size_t pos = 0; pos <= text.size();)
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
        {
            line = line.substr(0, hash);
        }
        line = Trim(line);
        if (line.empty())
        {
            continue;
        }
        if (line[0] == '[')
        {
            const std::string name = LowerStr(line);
            section = name == "[options]" ? kOptions : name == "[city]" ? kCitySection : name == "[menu]" ? kMenuSection : kOther;
            if (section == kOther)
            {
                fprintf(stderr, "gta2: %s:%d: unknown section %s\n", source, lineNo, line.c_str());
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            fprintf(stderr, "gta2: %s:%d: ignored (no '='): %s\n", source, lineNo, line.c_str());
            continue;
        }
        const std::string key = LowerStr(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        bool ok = false;
        if (section == kOptions)
        {
            const std::string v = LowerStr(value);
            if (key == "face_buttons" && (v == "auto" || v == "nintendo" || v == "xbox"))
            {
                gFaceMode = v == "auto" ? 0 : v == "nintendo" ? 1 : 2;
                ok = true;
            }
            else if (key == "left_stick" && (v == "dpad" || v == "off"))
            {
                gStickAsDpad = v == "dpad";
                ok = true;
            }
            else if (key == "stick_deadzone")
            {
                const double d = atof(v.c_str());
                ok = d >= 0.05 && d <= 0.95;
                if (ok)
                {
                    gStickDeadzone = static_cast<int>(d * 32767);
                }
            }
        }
        else if (section == kCitySection || section == kMenuSection)
        {
            const Layout layout = section == kCitySection ? kCity : kMenu;
            for (int b = 0; b < kButtonCount; ++b)
            {
                if (key == kButtonNames[b])
                {
                    ok = ParseAction(layout, value, &gBindings[layout][b]);
                    break;
                }
            }
        }
        if (!ok)
        {
            fprintf(stderr, "gta2: %s:%d: ignored: %s\n", source, lineNo, line.c_str());
        }
    }
}

void LoadControls()
{
    ParseControls(kDefaultControls, "defaults");
    const std::string path = gConfDir + "/controls.ini";
    FILE* f = fopen(path.c_str(), "r");
    if (!f)
    {
        if (FILE* w = fopen(path.c_str(), "w"))
        {
            fputs(kDefaultControls, w);
            fclose(w);
            fprintf(stderr, "gta2: wrote the default controller layout to %s\n", path.c_str());
        }
        return;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        text.append(buf, n);
    }
    fclose(f);
    ParseControls(text, path.c_str());
    fprintf(stderr, "gta2: controller layout from %s\n", path.c_str());
}

const Key* KeyForDik(int dik)
{
    for (const KeyRow& row : kKeyboard)
    {
        if (row.key.dik == dik)
        {
            return &row.key;
        }
    }
    return nullptr;
}

// The keys the game currently has for its 12 controls (the registry, or the defaults).
void RefreshSlotKeys()
{
    for (int s = 0; s < 12; ++s)
    {
        char name[4];
        snprintf(name, sizeof(name), "%d", s);
        const int dik = static_cast<int>(Gta2Native_RegistryDword(kControlKey, name, kDefaultSlotKeys[s]));
        const Key* k = KeyForDik(dik);
        gSlotKeys[s] = k ? *k : Key{dik, 0};
    }
}

int ButtonFromSdl(Uint8 b)
{
    switch (b)
    {
        // SDL's A/B/X/Y are positions: bottom, right, left, top.
        case SDL_CONTROLLER_BUTTON_A: return gNintendoLabels ? kB : kA;
        case SDL_CONTROLLER_BUTTON_B: return gNintendoLabels ? kA : kB;
        case SDL_CONTROLLER_BUTTON_X: return gNintendoLabels ? kY : kXBtn;
        case SDL_CONTROLLER_BUTTON_Y: return gNintendoLabels ? kXBtn : kY;
        case SDL_CONTROLLER_BUTTON_BACK: return kSelect;
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

void Add(std::set<Key>& keys, int input, Layout layout)
{
    // The stick acts as the d-pad button in the same direction.
    if (input >= kButtonCount)
    {
        input = kPadUp + (input - kStickUp);
    }
    const Action& a = gBindings[layout][input];
    if (a.kind == kSlot)
    {
        keys.insert(gSlotKeys[a.slot]);
    }
    else if (a.kind == kFixed)
    {
        keys.insert(a.key);
    }
}

void Apply(const std::set<Key>& keys)
{
    for (const Key& k : gHeld)
    {
        if (!keys.count(k))
        {
            if (gLogInput)
            {
                fprintf(stderr, "gta2: input: key 0x%02x up\n", k.dik);
            }
            gta2_port_key(k.dik, k.vk, 0, 0);
        }
    }
    for (const Key& k : keys)
    {
        if (!gHeld.count(k))
        {
            if (gLogInput)
            {
                fprintf(stderr, "gta2: input: key 0x%02x down\n", k.dik);
            }
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
        fprintf(stderr, "gta2: controller \"%s\"\n", SDL_GameControllerName(c));
        if (gLogInput)
        {
            char* mapping = SDL_GameControllerMapping(c);
            fprintf(stderr, "gta2: input: mapping %s\n", mapping ? mapping : "(none)");
            SDL_free(mapping);
        }
    }
}
} // namespace

extern "C" {

// main.cpp, before the window: where controls.ini and gamecontrollerdb.txt are.
void Gta2Native_SetConfDir(const char* dir)
{
    gConfDir = dir && *dir ? dir : ".";
}

// The SDL button (position) that carries a label, for the soak driver's controller
// events (soak_native.cpp): 0 = A, 1 = B, 2 = X, 3 = Y; other SDL buttons pass through.
int Gta2Native_PadButtonForLabel(int sdlButtonAsLabel)
{
    if (!gNintendoLabels || sdlButtonAsLabel > SDL_CONTROLLER_BUTTON_Y)
    {
        return sdlButtonAsLabel;
    }
    static const int kNintendo[4] = {SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_Y,
                                     SDL_CONTROLLER_BUTTON_X};
    return kNintendo[sdlButtonAsLabel];
}

void Gta2Native_InputInit()
{
    LoadControls();
    gLogInput = getenv("GTA2_LOG_INPUT") && atoi(getenv("GTA2_LOG_INPUT")) != 0;
    if (const char* labels = getenv("GTA2_FACE_BUTTONS"))
    {
        gFaceMode = strcmp(labels, "xbox") == 0 ? 2 : strcmp(labels, "nintendo") == 0 ? 1 : 0;
    }
    const char* launcherMapping = getenv("SDL_GAMECONTROLLERCONFIG");
    const bool fromLauncher = launcherMapping && *launcherMapping;
    gNintendoLabels = gFaceMode == 1 || (gFaceMode == 0 && !fromLauncher);
    fprintf(stderr, "gta2: controller face buttons: %s%s, left stick %s\n",
            gNintendoLabels ? "Nintendo (A right, B bottom) labels" : "as the mapping names them",
            gFaceMode == 0 ? (fromLauncher ? " (auto: launcher mapping)" : " (auto: no launcher mapping)") : "",
            gStickAsDpad ? "as d-pad" : "off");
    const std::string db = gConfDir + "/gamecontrollerdb.txt";
    const int added = SDL_GameControllerAddMappingsFromFile(db.c_str());
    if (added > 0)
    {
        fprintf(stderr, "gta2: %d controller mappings from %s\n", added, db.c_str());
    }
    RefreshSlotKeys();
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        OpenController(i);
    }
}

void Gta2Native_InputEvent(const SDL_Event& e)
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
            if (gLogInput)
            {
                const int label = ButtonFromSdl(e.cbutton.button);
                fprintf(stderr, "gta2: input: SDL button %s %s -> %s, layout %s\n",
                        SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(e.cbutton.button)),
                        e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up",
                        label >= 0 && label < kButtonCount ? kButtonNames[label] : "(unused)",
                        gPlaying == 1 ? "city" : "menu");
            }
            if (const int b = ButtonFromSdl(e.cbutton.button); b >= 0)
            {
                gPressed[b] = e.type == SDL_CONTROLLERBUTTONDOWN;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
        {
            const int v = e.caxis.value;
            switch (e.caxis.axis)
            {
                case SDL_CONTROLLER_AXIS_LEFTX:
                    gPressed[kStickLeft] = gStickAsDpad && v < -gStickDeadzone;
                    gPressed[kStickRight] = gStickAsDpad && v > gStickDeadzone;
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    gPressed[kStickUp] = gStickAsDpad && v < -gStickDeadzone;
                    gPressed[kStickDown] = gStickAsDpad && v > gStickDeadzone;
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
}

// Once per loop iteration, after the events: turns the controller's state into keys.
void Gta2Native_InputFrame()
{
    // The quit prompt takes the menu layout: A quits, B or Start carries on.
    const int playing = Gta2Native_IsPlaying() && !Gta2Native_QuitPromptShown() ? 1 : 0;
    if (playing != gPlaying)
    {
        if (gPlaying != -1)
        {
            for (int b = 0; b < kInputCount; ++b)
            {
                gBlocked[b] = gPressed[b];
            }
        }
        if (playing)
        {
            RefreshSlotKeys(); // the game's settings may have changed in the menus
        }
        gPlaying = playing;
        if (gLogInput)
        {
            fprintf(stderr, "gta2: input: layout %s\n", playing ? "city" : "menu");
        }
    }
    std::set<Key> keys;
    for (int b = 0; b < kInputCount; ++b)
    {
        if (!gPressed[b])
        {
            gBlocked[b] = false;
        }
        if (gPressed[b] && !gBlocked[b])
        {
            Add(keys, b, playing ? kCity : kMenu);
        }
    }
    Apply(keys);
}

} // extern "C"
#endif
