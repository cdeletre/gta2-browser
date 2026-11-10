// An on-screen keyboard for the controller, drawn over everything with the overlay font
// (render/gles_renderer.cpp, through options_native.cpp), for text the game or the port asks
// for when there is no keyboard:
//   - the front end's player name and password (Frontend states 3 and 5): it opens by itself
//     while the game waits for them, and types into the game as a keyboard would, a key down
//     then up per character (Frontend::HandlePlayerNameTyping_4B2F60 reads the DirectInput key
//     states and wants a release between two presses); the text shown is the game's own;
//   - the port's own fields (gta2-mp's lobby: name, room), through Gta2Native_OskStart.
// D-pad: move; A: type the key; B: delete; X: space; Y: upper/lower case; Start: done
// (Return); Select: cancel (Escape). A keyboard still types directly.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <deque>
#include <string>

#include "Frontend.hpp"

extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba);
extern "C" void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);

namespace
{
// A character's DirectInput key (a US layout, as the port's keyboard uses) and Win32 key.
struct KeyOf
{
    char c;
    int dik;
    int vk;
};
const KeyOf kKeys[] = {
    {'A', 0x1E, 'A'}, {'B', 0x30, 'B'}, {'C', 0x2E, 'C'}, {'D', 0x20, 'D'}, {'E', 0x12, 'E'}, {'F', 0x21, 'F'},
    {'G', 0x22, 'G'}, {'H', 0x23, 'H'}, {'I', 0x17, 'I'}, {'J', 0x24, 'J'}, {'K', 0x25, 'K'}, {'L', 0x26, 'L'},
    {'M', 0x32, 'M'}, {'N', 0x31, 'N'}, {'O', 0x18, 'O'}, {'P', 0x19, 'P'}, {'Q', 0x10, 'Q'}, {'R', 0x13, 'R'},
    {'S', 0x1F, 'S'}, {'T', 0x14, 'T'}, {'U', 0x16, 'U'}, {'V', 0x2F, 'V'}, {'W', 0x11, 'W'}, {'X', 0x2D, 'X'},
    {'Y', 0x15, 'Y'}, {'Z', 0x2C, 'Z'}, {'1', 0x02, '1'}, {'2', 0x03, '2'}, {'3', 0x04, '3'}, {'4', 0x05, '4'},
    {'5', 0x06, '5'}, {'6', 0x07, '6'}, {'7', 0x08, '7'}, {'8', 0x09, '8'}, {'9', 0x0A, '9'}, {'0', 0x0B, '0'},
    {'-', 0x0C, 0xBD}, {'.', 0x34, 0xBE}, {' ', 0x39, 0x20},
};
constexpr int kDikReturn = 0x1C, kVkReturn = 0x0D, kDikBack = 0x0E, kVkBack = 0x08, kDikEscape = 0x01, kVkEscape = 0x1B;
constexpr int kDikShift = 0x2A, kVkShift = 0x10;

const KeyOf* Find(char c)
{
    const char u = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    for (const KeyOf& k : kKeys)
        if (k.c == u)
            return &k;
    return nullptr;
}

// The grid: 10 keys a row; the last row's specials are 2 or more columns wide.
const char* const kRows[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-.  ", "1234567890"};
enum Special
{
    kCase,
    kSpace,
    kDelete,
    kDone,
    kSpecialCount
};
const char* const kSpecialLabels[kSpecialCount] = {"aA", "Space", "Del", "OK"};
const int kSpecialCols[kSpecialCount] = {0, 2, 6, 8}; // first column of each, 2 columns wide (space: 4)

enum Mode
{
    kClosed,
    kGameKeys, // types into the front end
    kField     // edits a string of the port's
};
Mode gMode = kClosed;
int gRow = 0;
int gCol = 0;
bool gLower = false;
std::string gTitle;
std::string gText; // kField: the text; kGameKeys: what was typed (the password, not readable back)
size_t gMax = 16;
void (*gDone)(bool ok, const char* text) = nullptr;
bool gDismissed = false; // kGameKeys: closed by the player for this typing (a keyboard is used)

// kGameKeys: presses waiting to reach the game, one a few frames (down, then up)
struct Press
{
    int dik, vk, ch;
    bool shift;
};
std::deque<Press> gQueue;
int gPhase = 0; // 0 idle; 1-2 down; 3-4 up

int CurrentFrontendState()
{
    return gFrontend_67DC84 ? static_cast<int>(gFrontend_67DC84->field_110_state) : 0;
}

void Queue(int dik, int vk, int ch, bool shift = false)
{
    gQueue.push_back({dik, vk, ch, shift});
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

void Type(char c)
{
    if (gLower && c >= 'A' && c <= 'Z')
        c = static_cast<char>(c + 32);
    if (gMode == kField)
    {
        if (gText.size() < gMax)
            gText += c;
        return;
    }
    if (const KeyOf* k = Find(c))
    {
        Queue(k->dik, k->vk, static_cast<unsigned char>(c), c >= 'a' && c <= 'z');
        if (gText.size() < gMax)
            gText += c;
    }
}

void Delete()
{
    if (!gText.empty())
        gText.pop_back();
    if (gMode == kGameKeys)
        Queue(kDikBack, kVkBack, 8);
}

void Done()
{
    if (gMode == kGameKeys)
    {
        Queue(kDikReturn, kVkReturn, 13);
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
        Queue(kDikEscape, kVkEscape, 27);
        gDismissed = true;
        gMode = kClosed;
        fprintf(stderr, "gta2: keyboard: cancelled\n");
        return;
    }
    Close(false);
}

// The key under the cursor: a character, or a special (-1 - index).
int KeyAt(int row, int col)
{
    if (row < 4)
        return kRows[row][col] == ' ' ? 0 : kRows[row][col];
    for (int s = kSpecialCount - 1; s >= 0; --s)
        if (col >= kSpecialCols[s])
            return -1 - s;
    return -1;
}

void PressKey(int key)
{
    if (key > 0)
        Type(static_cast<char>(key));
    else if (key == -1 - kCase)
        gLower = !gLower;
    else if (key == -1 - kSpace)
        Type(' ');
    else if (key == -1 - kDelete)
        Delete();
    else if (key == -1 - kDone)
        Done();
}

void Move(int dr, int dc)
{
    for (int i = 0; i < 12; ++i)
    {
        gRow = (gRow + dr + 5) % 5;
        gCol = (gCol + dc + 10) % 10;
        if (KeyAt(gRow, gCol) != 0)
            break;
        if (dr == 0 && dc == 0)
            dc = 1;
    }
    // on the specials' row, sit on each special's first column
    if (gRow == 4)
    {
        const int s = -1 - KeyAt(4, gCol);
        gCol = kSpecialCols[s];
    }
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
} // namespace

extern "C" {

bool Gta2Native_OskOpen()
{
    return gMode != kClosed;
}

// The port's own text field: done(true, text) on OK, done(false, ...) on cancel.
void Gta2Native_OskStart(const char* title, const char* initial, int maxLen, void (*done)(bool ok, const char* text))
{
    gMode = kField;
    gTitle = title ? title : "";
    gText = initial ? initial : "";
    gMax = maxLen > 0 ? static_cast<size_t>(maxLen) : 16;
    if (gText.size() > gMax)
        gText.resize(gMax);
    gDone = done;
    gRow = 0;
    gCol = 0;
    fprintf(stderr, "gta2: keyboard: %s\n", gTitle.c_str());
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
        gMode = kGameKeys;
        gTitle = state == FrontendState::Unknown_5 ? "Password" : "Player name";
        gText.clear();
        gMax = 8;
        gRow = 0;
        gCol = 0;
        fprintf(stderr, "gta2: keyboard: %s\n", gTitle.c_str());
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
    switch (action)
    {
        case 0: Move(-1, 0); break;
        case 1: Move(1, 0); break;
        case 2:
            if (gRow == 4)
            {
                const int s = -1 - KeyAt(4, gCol);
                gCol = kSpecialCols[(s + kSpecialCount - 1) % kSpecialCount];
            }
            else
                Move(0, -1);
            break;
        case 3:
            if (gRow == 4)
            {
                const int s = -1 - KeyAt(4, gCol);
                gCol = kSpecialCols[(s + 1) % kSpecialCount];
            }
            else
                Move(0, 1);
            break;
        case 4: PressKey(KeyAt(gRow, gCol)); break;
        case 5: Delete(); break;
        case 6: Type(' '); break;
        case 7: gLower = !gLower; break;
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
    else if (ch >= 0x20 && ch < 0x7f && (gMode == kField || Find(static_cast<char>(ch))))
    {
        const bool lower = gLower;
        gLower = ch >= 'a' && ch <= 'z';
        Type(static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch));
        gLower = lower;
    }
}

void Gta2Native_OskDraw(int dw, int dh)
{
    if (gMode == kClosed)
        return;
    constexpr int kCols = 40;
    constexpr int kLines = 13;
    const float px = static_cast<float>(std::max(1, std::min(dh / (kLines * 14 + 8), dw / (kCols * 6 + 8))));
    const float cell = 6 * px;
    const float lineH = 14 * px;
    const uint32_t kWhite = 0xffffffff, kGrey = 0xffa0a0a0, kYellow = 0xff30d0ff, kKey = 0xff383838, kSel = 0xff30a0d0;
    const float width = kCols * cell;
    const float x0 = (dw - width) / 2;
    float y = (dh - kLines * lineH) / 2;
    Gta2Native_OverlayRect(x0 - 2 * cell, y - lineH, width + 4 * cell, (kLines + 1.5f) * lineH, 0xf0101010);
    Gta2Native_OverlayText(x0, y, px, kGrey, gTitle.c_str());
    y += lineH;
    // the text (the game's own name while it types it), with a cursor
    std::string text = gMode == kGameKeys && gTitle == "Player name" ? GameName() : gText;
    if (gMode == kGameKeys && gTitle == "Password")
        text = std::string(gText.size(), '*');
    Gta2Native_OverlayRect(x0 - cell, y - 3 * px, width + 2 * cell, lineH, 0xff000000);
    Gta2Native_OverlayText(x0, y, px, kWhite, (text + "_").c_str());
    y += 1.5f * lineH;
    // the grid: 10 keys a row, each 4 columns wide
    const float keyW = 4 * cell;
    for (int r = 0; r < 5; ++r, y += lineH)
    {
        if (r < 4)
        {
            for (int c = 0; c < 10; ++c)
            {
                char ch = kRows[r][c];
                if (ch == ' ')
                    continue;
                if (gLower && ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch + 32);
                const bool sel = r == gRow && c == gCol;
                const float kx = x0 + c * keyW;
                Gta2Native_OverlayRect(kx, y - 3 * px, keyW - px, lineH - px, sel ? kSel : kKey);
                const char s[2] = {ch, 0};
                Gta2Native_OverlayText(kx + 1.5f * cell, y, px, sel ? kWhite : kWhite, s);
            }
            continue;
        }
        for (int sp = 0; sp < kSpecialCount; ++sp)
        {
            const int span = sp + 1 < kSpecialCount ? kSpecialCols[sp + 1] - kSpecialCols[sp] : 10 - kSpecialCols[sp];
            const bool sel = gRow == 4 && gCol == kSpecialCols[sp];
            const float kx = x0 + kSpecialCols[sp] * keyW;
            Gta2Native_OverlayRect(kx, y - 3 * px, span * keyW - px, lineH - px, sel ? kSel : kKey);
            const char* label = sp == kCase ? (gLower ? "ABC" : "abc") : kSpecialLabels[sp];
            const float tw = static_cast<float>(strlen(label)) * cell;
            Gta2Native_OverlayText(kx + (span * keyW - tw) / 2, y, px, kWhite, label);
        }
    }
    y += 0.5f * lineH;
    Gta2Native_OverlayText(x0, y, px, kGrey, "A: type  B: delete  X: space  Y: case");
    Gta2Native_OverlayText(x0, y + lineH, px, kYellow, "Start: done  Select: cancel");
}

} // extern "C"
#endif
