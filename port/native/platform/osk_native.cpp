// An on-screen keyboard for the controller, drawn over everything with the overlay font
// (render/gles_renderer.cpp, through options_native.cpp), for text the game or the port asks
// for when there is no keyboard:
//   - the front end's player name and password (Frontend states 3 and 5): it opens by itself
//     while the game waits for them, and types into the game as a keyboard would, a key down
//     then up per character (Frontend::HandlePlayerNameTyping_4B2F60 reads the DirectInput key
//     states and wants a release between two presses); the text shown is the game's own;
//   - the port's own fields (gta2-mp's lobby: name, room), through Gta2Native_OskStart.
// A QWERTY layout:
//     1 2 3 4 5 6 7 8 9 0 - = [Back]
//      Q W E R T Y U I O P
//       A S D F G H J K L [OK]
//   [Shift] Z X C V B N M
// Shift turns between lower case, upper case for the next key, and upper case kept (its key
// lights up in two colours); shifted, the top row is ! @ # $ % ? & * ( ) _ +.
// D-pad: move; A: press the key; B: delete; X: space; Y: shift; Start: done (Return);
// Select: cancel (Escape). A keyboard still types directly.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "Frontend.hpp"

extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba);
extern "C" void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);
extern "C" void Gta2Native_OverlayTextExact(float x, float y, float px, uint32_t rgba, const char* text);

namespace
{
enum Special
{
    kChar,
    kBack,
    kOk,
    kShift
};

// A key: what it types unshifted and shifted, and how a keyboard types each (US layout, as
// the port's keyboard uses: DirectInput key and Win32 key; shifted, with Shift held).
struct Key
{
    char lo, hi;
    int dik, vk;
    int hiDik;  // the key typed with Shift for `hi`
    float x, w; // position and width in key widths
    Special special;
};

std::vector<std::vector<Key>> MakeLayout()
{
    std::vector<std::vector<Key>> rows(4);
    const char* top = "1234567890-=";
    const char* topShifted = "!@#$%?&*()_+";
    const int topDik[12] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    const int topVk[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xBD, 0xBB};
    for (int i = 0; i < 12; ++i)
    {
        // '?' is Shift + '/' on a US keyboard; the others Shift + their own key
        rows[0].push_back({top[i], topShifted[i], topDik[i], topVk[i], i == 5 ? 0x35 : topDik[i], float(i), 1, kChar});
    }
    rows[0].push_back({0, 0, 0x0E, 0x08, 0x0E, 12, 2, kBack});
    struct L
    {
        char c;
        int dik;
    };
    const L r1[] = {{'q', 0x10}, {'w', 0x11}, {'e', 0x12}, {'r', 0x13}, {'t', 0x14},
                    {'y', 0x15}, {'u', 0x16}, {'i', 0x17}, {'o', 0x18}, {'p', 0x19}};
    const L r2[] = {{'a', 0x1E}, {'s', 0x1F}, {'d', 0x20}, {'f', 0x21}, {'g', 0x22},
                    {'h', 0x23}, {'j', 0x24}, {'k', 0x25}, {'l', 0x26}};
    const L r3[] = {{'z', 0x2C}, {'x', 0x2D}, {'c', 0x2E}, {'v', 0x2F}, {'b', 0x30}, {'n', 0x31}, {'m', 0x32}};
    for (int i = 0; i < 10; ++i)
        rows[1].push_back({r1[i].c, char(r1[i].c - 32), r1[i].dik, r1[i].c - 32, r1[i].dik, 0.5f + i, 1, kChar});
    for (int i = 0; i < 9; ++i)
        rows[2].push_back({r2[i].c, char(r2[i].c - 32), r2[i].dik, r2[i].c - 32, r2[i].dik, 1.0f + i, 1, kChar});
    rows[2].push_back({0, 0, 0x1C, 0x0D, 0x1C, 10.0f, 2, kOk});
    rows[3].push_back({0, 0, 0x2A, 0x10, 0x2A, 0, 1.5f, kShift});
    for (int i = 0; i < 7; ++i)
        rows[3].push_back({r3[i].c, char(r3[i].c - 32), r3[i].dik, r3[i].c - 32, r3[i].dik, 1.5f + i, 1, kChar});
    return rows;
}
const std::vector<std::vector<Key>>& Layout()
{
    static const std::vector<std::vector<Key>> rows = MakeLayout();
    return rows;
}
constexpr float kLayoutWidth = 14; // key widths across

constexpr int kDikReturn = 0x1C, kVkReturn = 0x0D, kDikBack = 0x0E, kVkBack = 0x08, kDikEscape = 0x01, kVkEscape = 0x1B;
constexpr int kDikShift = 0x2A, kVkShift = 0x10, kDikSpace = 0x39, kVkSpace = 0x20;

enum Mode
{
    kClosed,
    kGameKeys, // types into the front end
    kField     // edits a string of the port's
};
Mode gMode = kClosed;
int gRow = 1;
int gCol = 0;
int gShift = 0; // 0 lower case, 1 upper case for the next key, 2 upper case kept
std::string gTitle;
std::string gText; // kField: the text; kGameKeys: what was typed (the password, not readable back)
size_t gMax = 16;
void (*gDone)(bool ok, const char* text) = nullptr;
bool gDismissed = false; // kGameKeys: closed by the player for this typing

// kGameKeys: presses waiting to reach the game, one a few frames (down, then up)
struct Press
{
    int dik, vk, ch;
    bool shift;
};
std::deque<Press> gQueue;
int gPhase = 0;

int CurrentFrontendState()
{
    return gFrontend_67DC84 ? static_cast<int>(gFrontend_67DC84->field_110_state) : 0;
}

void Close(bool ok)
{
    const Mode was = gMode;
    gMode = kClosed;
    fprintf(stderr, "gta2: keyboard: %s\n", ok ? "done" : "cancelled");
    if (was == kField && gDone)
    {
        auto done = gDone;
        gDone = nullptr;
        done(ok, gText.c_str());
    }
}

// A character into the text (and into the game, through its keys).
void Type(char c, int dik, int vk, bool shift)
{
    if (gText.size() < gMax)
        gText += c;
    if (gMode == kGameKeys)
        gQueue.push_back({dik, vk, static_cast<unsigned char>(c), shift});
}

void TypeKey(const Key& k)
{
    const bool up = gShift != 0;
    Type(up ? k.hi : k.lo, up ? k.hiDik : k.dik, k.vk, up);
    if (gShift == 1)
        gShift = 0; // upper case for this key only
}

void Delete()
{
    if (!gText.empty())
        gText.pop_back();
    if (gMode == kGameKeys)
        gQueue.push_back({kDikBack, kVkBack, 8, false});
}

void Done()
{
    if (gMode == kGameKeys)
    {
        gQueue.push_back({kDikReturn, kVkReturn, 13, false});
        gDismissed = true; // the game leaves the typing state on Return
        gMode = kClosed;
        fprintf(stderr, "gta2: keyboard: done\n");
        return;
    }
    Close(true);
}

void Cancel()
{
    if (gMode == kGameKeys)
    {
        gQueue.push_back({kDikEscape, kVkEscape, 27, false});
        gDismissed = true;
        gMode = kClosed;
        fprintf(stderr, "gta2: keyboard: cancelled\n");
        return;
    }
    Close(false);
}

void PressSelected()
{
    const Key& k = Layout()[gRow][gCol];
    switch (k.special)
    {
        case kChar: TypeKey(k); break;
        case kBack: Delete(); break;
        case kOk: Done(); break;
        case kShift: gShift = (gShift + 1) % 3; break;
    }
}

// Up or down: the key of the next row nearest under this one's centre.
void MoveRow(int dr)
{
    const auto& rows = Layout();
    const Key& k = rows[gRow][gCol];
    const float centre = k.x + k.w / 2;
    gRow = (gRow + dr + 4) % 4;
    int best = 0;
    float bestD = 1e9f;
    for (size_t i = 0; i < rows[gRow].size(); ++i)
    {
        const float d = fabsf(rows[gRow][i].x + rows[gRow][i].w / 2 - centre);
        if (d < bestD)
        {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    gCol = best;
}

// The game's player name as it stands (wide characters, as typed so far).
std::string GameName()
{
    std::string out;
    if (!gFrontend_67DC84)
        return out;
    const int n = std::min<int>(gFrontend_67DC84->field_C9B2_curr_plyr_name_length, 8);
    for (int i = 0; i < n; ++i)
    {
        const wchar_t c = gFrontend_67DC84->field_C9A0_curr_plyr_name[i];
        out += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?';
    }
    return out;
}

void Open(Mode mode, const std::string& title, const std::string& text, size_t max)
{
    gMode = mode;
    gTitle = title;
    gText = text.substr(0, max);
    gMax = max;
    gRow = 1; // on Q
    gCol = 0;
    gShift = 0;
    fprintf(stderr, "gta2: keyboard: %s\n", gTitle.c_str());
}
} // namespace

extern "C" {

bool Gta2Native_OskOpen()
{
    return gMode != kClosed;
}

// The port's own text field: done(true, text) on OK, done(false, ...) on cancel.
void Gta2Native_OskStart(const char* title, const char* initial, int maxLen, void (*done)(bool ok, const char* text))
{
    Open(kField, title ? title : "", initial ? initial : "", maxLen > 0 ? static_cast<size_t>(maxLen) : 16);
    gDone = done;
}

// Once per frame (input_sdl.cpp): opens for the front end's typing, and feeds it the presses.
void Gta2Native_OskFrame()
{
    const int state = CurrentFrontendState();
    const bool typing = state == FrontendState::User_Typing_New_Player_Name_3 || state == FrontendState::Unknown_5;
    if (!typing)
    {
        gDismissed = false;
        if (gMode == kGameKeys)
            gMode = kClosed; // the game has left its name or password entry
    }
    else if (gMode == kClosed && !gDismissed && gQueue.empty())
    {
        Open(kGameKeys, state == FrontendState::Unknown_5 ? "Password" : "Player name", "", 8);
    }
    // one press at a time: down for 2 frames, up for 2 (the game wants a release in between)
    if (gQueue.empty())
        return;
    const Press& p = gQueue.front();
    ++gPhase;
    if (gPhase == 1)
    {
        if (p.shift)
            gta2_port_key(kDikShift, kVkShift, 1, 0);
        gta2_port_key(p.dik, p.vk, 1, p.ch);
    }
    else if (gPhase == 3)
    {
        gta2_port_key(p.dik, p.vk, 0, 0);
        if (p.shift)
            gta2_port_key(kDikShift, kVkShift, 0, 0);
    }
    else if (gPhase >= 4)
    {
        gPhase = 0;
        gQueue.pop_front();
    }
}

// input_sdl.cpp: 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 X, 7 Y, 8 Start, 9 Select.
void Gta2Native_OskInput(int action)
{
    if (gMode == kClosed)
        return;
    const int n = static_cast<int>(Layout()[gRow].size());
    switch (action)
    {
        case 0: MoveRow(-1); break;
        case 1: MoveRow(1); break;
        case 2: gCol = (gCol + n - 1) % n; break;
        case 3: gCol = (gCol + 1) % n; break;
        case 4: PressSelected(); break;
        case 5: Delete(); break;
        case 6: Type(' ', kDikSpace, kVkSpace, false); break;
        case 7: gShift = (gShift + 1) % 3; break;
        case 8: Done(); break;
        case 9: Cancel(); break;
        default: break;
    }
}

// A physical keyboard while the on-screen one is up: printable characters, Backspace,
// Return, Escape (input_sdl.cpp).
void Gta2Native_OskChar(int ch)
{
    if (gMode == kClosed)
        return;
    if (ch == 8)
        Delete();
    else if (ch == 13)
        Done();
    else if (ch == 27)
        Cancel();
    else if (ch == ' ')
        Type(' ', kDikSpace, kVkSpace, false);
    else
    {
        for (const auto& row : Layout())
            for (const Key& k : row)
                if (k.special == kChar && (k.lo == ch || k.hi == ch))
                {
                    const bool up = k.hi == ch && k.lo != ch;
                    Type(static_cast<char>(ch), up ? k.hiDik : k.dik, k.vk, up);
                    return;
                }
        if (gMode == kField && ch >= 0x20 && ch < 0x7f)
            Type(static_cast<char>(ch), 0, 0, false);
    }
}

void Gta2Native_OskDraw(int dw, int dh)
{
    if (gMode == kClosed)
        return;
    constexpr int kCols = 56; // characters across: 14 keys of 4
    constexpr int kLines = 10;
    const float px = static_cast<float>(std::max(1, std::min(dh / (kLines * 16 + 8), dw / (kCols * 6 + 16))));
    const float cell = 6 * px;
    const float lineH = 16 * px;
    const uint32_t kWhite = 0xffffffff, kGrey = 0xffa0a0a0, kYellow = 0xff30d0ff, kKey = 0xff383838, kSel = 0xff30a0d0,
                   kShiftOnce = 0xff2a7a2a, kShiftLock = 0xff2a2aa0;
    const float keyW = 4 * cell;
    const float width = kLayoutWidth * keyW;
    const float x0 = (dw - width) / 2;
    float y = (dh - kLines * lineH) / 2;
    Gta2Native_OverlayRect(x0 - 2 * cell, y - lineH, width + 4 * cell, (kLines + 1.5f) * lineH, 0xf0101010);
    Gta2Native_OverlayText(x0, y, px, kGrey, gTitle.c_str());
    y += lineH;
    // the text (the game's own name while it types it), with a cursor
    std::string text = gMode == kGameKeys && gTitle == "Player name" ? GameName() : gText;
    if (gMode == kGameKeys && gTitle == "Password")
        text = std::string(gText.size(), '*');
    Gta2Native_OverlayRect(x0 - cell, y - 4 * px, width + 2 * cell, lineH, 0xff000000);
    Gta2Native_OverlayTextExact(x0, y, px, kWhite, (text + "_").c_str());
    y += 1.5f * lineH;
    const auto& rows = Layout();
    for (size_t r = 0; r < rows.size(); ++r, y += lineH)
    {
        for (size_t c = 0; c < rows[r].size(); ++c)
        {
            const Key& k = rows[r][c];
            const bool sel = static_cast<int>(r) == gRow && static_cast<int>(c) == gCol;
            uint32_t bg = sel ? kSel : kKey;
            if (k.special == kShift && gShift)
                bg = sel ? kSel : gShift == 1 ? kShiftOnce : kShiftLock;
            const float kx = x0 + k.x * keyW;
            const float kw = k.w * keyW - px;
            Gta2Native_OverlayRect(kx, y - 4 * px, kw, lineH - 2 * px, bg);
            if (k.special == kShift && gShift && sel)
                Gta2Native_OverlayRect(kx, y + lineH - 6 * px, kw, 2 * px, gShift == 1 ? kShiftOnce : kShiftLock);
            char label[8] = {0};
            switch (k.special)
            {
                case kChar: label[0] = gShift ? k.hi : k.lo; break;
                case kBack: strcpy(label, "Back"); break;
                case kOk: strcpy(label, "OK"); break;
                case kShift: strcpy(label, gShift == 2 ? "SHIFT" : "Shift"); break;
            }
            const float tw = static_cast<float>(strlen(label)) * cell;
            Gta2Native_OverlayTextExact(kx + (kw - tw) / 2, y, px, kWhite, label);
        }
    }
    y += 0.5f * lineH;
    Gta2Native_OverlayText(x0, y, px, kGrey, "A: press  B: delete  X: space  Y: shift");
    Gta2Native_OverlayText(x0, y + lineH, px, kYellow, "Start: done  Select: cancel");
}

} // extern "C"
#endif
