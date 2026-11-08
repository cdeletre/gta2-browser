// Native entry point (Linux aarch64): SDL2 window + GLES 2 renderer (jalon 3), or no
// window at all in GTA2_HEADLESS builds. Sound through SDL audio (audio/audio_sdl.cpp).
//
// Does what web/shell.js and port/platform/main_web.cpp do in the browser:
// registry defaults, the original's C runtime start-up (Gta2Port_StaticInit),
// WinMain's initialisation, then the message loop in slices (patch 0007).
//
// Environment:
// Folders (defaults relative to the binary, i.e. the port folder roms/ports/gta2/):
//   GTA2_DATA_DIR     the installed game files, the folder holding data/ (default gamedata/)
//   GTA2_SAVE_DIR     what the game writes to player\: profiles, church saves, high scores
//                     (default saves/; platform/fs_native.cpp redirects player\ there)
//   GTA2_CONF_DIR     settings: registry.txt (the game's registry), controls.ini (default conf/)
// Other:
//   GTA2_AUTOSTART=1  press Enter twice like ?autostart=1 (PLAY, then area 1: Downtown)
//   GTA2_MENU_KEYS    the menu presses instead, comma-separated, 1 s apart, from 0.5 s after
//                     the menu has drawn 30 frames:
//                     ok, up, down, left, right, back, start, select ("ok,right,ok": area 2;
//                     "ok,up,up,ok": resume the saved game)
//   GTA2_RUN_SECONDS  stop cleanly after this many seconds (soak tests)
//   GTA2_STATUS_SECONDS  seconds between status lines in the log (default 60, 0: none)
//   GTA2_SOAK=1       random play once the player exists (walk, drive, shoot; soak_native.cpp)
//   GTA2_SOAK_SEED    the soak's random sequence (default 1)
//   GTA2_SOAK_INPUT   direct (default), keyboard or pad: also for the autostart's presses
//   GTA2_TEST         save or phone: test scenarios (platform/scenario_native.cpp)
// Window, vsync and screenshots: see platform/window_sdl.cpp.
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <emscripten.h>
#include <windows.h>

int __stdcall WinMain_5E53F0(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
extern "C" int Gta2Port_RunLoopSlice();
extern "C" void Gta2Port_StaticInit();
extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" void gta2_port_reg_set_dword(const char* path, const char* name, DWORD value);
extern "C" void gta2_port_reg_set_string(const char* path, const char* name, const char* value);
extern "C" unsigned int gta2_port_present_count();
extern "C" void Gta2Native_StatusLine(char* out, int size); // platform/status_native.cpp
extern "C" int Gta2Native_TickCount(int* peds, int* cars);
extern "C" void Gta2Native_SoakInit(); // platform/soak_native.cpp
extern "C" void Gta2Native_SoakStep(double now);
extern "C" void Gta2Native_SoakSummary(char* out, int size);
extern "C" void Gta2Native_InputModeInit();
extern "C" void Gta2Native_AutostartKey(int which, bool down);
extern "C" void Gta2Native_ScenarioInit(); // platform/scenario_native.cpp
extern "C" void Gta2Native_ScenarioStep(double now);
extern "C" void Gta2Native_SetSaveDir(const char* dir);       // platform/fs_native.cpp
extern "C" int Gta2Native_RegistryLoad(const char* path);    // platform/registry_native.cpp
extern "C" bool Gta2Native_RegistrySave();
extern "C" void Gta2Native_AudioInit();     // audio/audio_sdl.cpp (audio_null.cpp headless)
extern "C" void Gta2Native_AudioPump();
extern "C" void Gta2Native_AudioShutdown();
extern "C" int gta2_port_set_view_width(int width);   // port/render/vid_web.cpp
extern "C" int gta2_port_set_view_height(int height); // port/render/vid_web.cpp (native only)
#if !defined(GTA2_NATIVE_HEADLESS)
extern "C" bool Gta2Native_WindowInit(); // platform/window_sdl.cpp
extern "C" void Gta2Native_SetConfDir(const char* dir); // platform/input_sdl.cpp
extern "C" void Gta2Native_DrawableSize(int* w, int* h);
extern "C" bool Gta2Native_PumpEvents();
extern "C" void Gta2Native_WindowShutdown();
#endif

namespace
{
constexpr int kRunning = 0x7FFFFFFF; // WinMain_5E53F0 under GTA2_PORT: "the loop continues"
const char* const kRegRoot = "hklm\\software\\dma design ltd\\gta2";

std::string BinaryDir()
{
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        return ".";
    }
    buf[n] = 0;
    std::string dir(buf);
    const size_t slash = dir.rfind('/');
    return slash == std::string::npos ? "." : dir.substr(0, slash);
}

// Same defaults as seedRegistry() in web/shell.js, except the controls: the native
// build keeps the original's own defaults (restored by patch 0008).
void SeedRegistry()
{
    const std::string screen = std::string(kRegRoot) + "\\screen";
    gta2_port_reg_set_dword(screen.c_str(), "start_mode", 0);
    gta2_port_reg_set_dword(screen.c_str(), "do_play_movie", 0); // no Bink playback
    // The original's own "minimum frame rate" option, which network games force on
    // (Init_FrameRateLightAndUnknown_5D8EB0): each tick is scheduled one frame after the
    // previous one was due, and the loop catches up (up to 8 ticks per draw). Off, each
    // tick is due one frame after it ran; with vsync at 60 Hz and a millisecond clock
    // about one tick in fifteen slips a display frame (27.3-28.7 ticks/s measured on the
    // console instead of 30).
    gta2_port_reg_set_dword(screen.c_str(), "min_frame_rate", 1);
    // The 10.5 code is the freeloader.com edition: 2 unlocks all three districts.
    gta2_port_reg_set_dword("hklm\\software\\freeloader.com\\gta2", "cityinstalled", 2);
    gta2_port_reg_set_string((std::string(kRegRoot) + "\\option").c_str(), "language", "e");
}

// mkdir -p
bool MakeDirs(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            const std::string part = path.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
            {
                fprintf(stderr, "gta2: cannot create %s: %s\n", part.c_str(), strerror(errno));
                return false;
            }
        }
    }
    return true;
}

std::string Absolute(const std::string& path)
{
    char buf[PATH_MAX];
    return realpath(path.c_str(), buf) ? std::string(buf) : path;
}

std::string DirFromEnv(const char* name, const std::string& base, const char* sub)
{
    const char* env = getenv(name);
    return env && *env ? std::string(env) : base + "/" + sub;
}

void SleepMs(int ms)
{
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// Enter down at `at` ms, up 100 ms later (as the web shell's ?autostart=1).
struct Tap
{
    double at;
    int stage;
    int which; // GTA2_MENU_KEYS key
};
} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string base = BinaryDir();
    const std::string dataDir = Absolute(DirFromEnv("GTA2_DATA_DIR", base, "gamedata"));
    if (!MakeDirs(DirFromEnv("GTA2_SAVE_DIR", base, "saves")) || !MakeDirs(DirFromEnv("GTA2_CONF_DIR", base, "conf")))
    {
        return 1;
    }
    const std::string saveDir = Absolute(DirFromEnv("GTA2_SAVE_DIR", base, "saves"));
    const std::string confDir = Absolute(DirFromEnv("GTA2_CONF_DIR", base, "conf"));
    if (chdir(dataDir.c_str()) != 0)
    {
        fprintf(stderr, "gta2: cannot open the game folder %s\n", dataDir.c_str());
        return 1;
    }
    fprintf(stderr, "gta2: game files %s, saves %s, settings %s\n", dataDir.c_str(), saveDir.c_str(), confDir.c_str());
    Gta2Native_SetSaveDir(saveDir.c_str());

#if !defined(GTA2_NATIVE_HEADLESS)
    Gta2Native_SetConfDir(confDir.c_str()); // controls.ini, gamecontrollerdb.txt
    if (!Gta2Native_WindowInit())
    {
        return 1;
    }
    // Gameplay frames take the screen's shape: wider than 4:3, they are 480 high and as wide
    // as the shape (web/shell.js chooseViewWidth: 852 at 16:9); taller than 4:3, they are
    // 640 wide and as tall as the shape (720x720: 640x640). The 4:3 box keeps its scale
    // either way. Menus stay 640x480, letter- or pillarboxed.
    int dw = 640;
    int dh = 480;
    Gta2Native_DrawableSize(&dw, &dh);
    int viewWidth = 640;
    int viewHeight = 480;
    if (dw > 0 && dh > 0 && dw * 3 >= dh * 4)
    {
        viewWidth = gta2_port_set_view_width(static_cast<int>(lround(480.0 * dw / dh)));
    }
    else if (dw > 0 && dh > 0)
    {
        viewHeight = gta2_port_set_view_height(static_cast<int>(lround(640.0 * dh / dw)));
    }
    fprintf(stderr, "gta2: gameplay view %dx%d\n", viewWidth, viewHeight);
#endif
    Gta2Native_AudioInit(); // before WinMain: the game opens its sound banks there

    SeedRegistry();
    // The saved registry overrides the defaults (as web/shell.js does with its stored copy),
    // except the movie switch: there is no Bink playback.
    Gta2Native_RegistryLoad((confDir + "/registry.txt").c_str());
    gta2_port_reg_set_dword((std::string(kRegRoot) + "\\screen").c_str(), "do_play_movie", 0);
    Gta2Port_StaticInit(); // what the original's C runtime start-up did

    static char cmd[256];
    std::string args;
    for (int i = 1; i < argc; ++i)
    {
        args += (i > 1 ? " " : "") + std::string(argv[i]);
    }
    strncpy(cmd, args.c_str(), sizeof(cmd) - 1);

    const int r = WinMain_5E53F0(reinterpret_cast<HINSTANCE>(0x400000), nullptr, cmd, SW_SHOWNORMAL);
    if (r != kRunning)
    {
        fprintf(stderr, "gta2: WinMain returned %d\n", r);
        return r;
    }

    const char* runEnv = getenv("GTA2_RUN_SECONDS");
    const double runMs = runEnv ? atof(runEnv) * 1000.0 : 0.0;
    std::vector<Tap> taps;
    {
        const char* keys = getenv("GTA2_MENU_KEYS");
        if (!keys && getenv("GTA2_AUTOSTART") && atoi(getenv("GTA2_AUTOSTART")) != 0)
        {
            keys = "ok,ok";
        }
        static const char* const kNames[] = {"ok", "up", "down", "left", "right", "back", "start", "select"};
        std::string list = keys ? keys : "";
        size_t pos = 0;
        while (pos < list.size())
        {
            size_t end = list.find(',', pos);
            end = end == std::string::npos ? list.size() : end;
            const std::string name = list.substr(pos, end - pos);
            int which = -1;
            for (int k = 0; k < 8; ++k)
            {
                which = name == kNames[k] ? k : which;
            }
            if (which < 0)
            {
                fprintf(stderr, "gta2: GTA2_MENU_KEYS: unknown key \"%s\"\n", name.c_str());
            }
            else
            {
                taps.push_back({500.0 + 1000.0 * taps.size(), 0, which});
            }
            pos = end + 1;
        }
    }
    Gta2Native_InputModeInit();
    const bool soak = getenv("GTA2_SOAK") && atoi(getenv("GTA2_SOAK")) != 0;
    if (soak)
    {
        Gta2Native_SoakInit();
    }
    Gta2Native_ScenarioInit();
    int lastTicks = 0;

    const double start = emscripten_get_now();
    double lastReport = start;
    double lastRegistrySave = 0.0;
    double menuReadyAt = -1.0; // when the menu presses' schedule starts
    const char* statusEnv = getenv("GTA2_STATUS_SECONDS");
    const double statusMs = (statusEnv ? atof(statusEnv) : 60.0) * 1000.0;
    int code = 0;
    // GTA2_LOG_TEXTURES=1: a loop iteration over 100 ms is logged with the time of each step.
    const bool logStalls = getenv("GTA2_LOG_TEXTURES") && atoi(getenv("GTA2_LOG_TEXTURES")) != 0;
    for (;;)
    {
        const double loopStart = emscripten_get_now();
        const double now = emscripten_get_now() - start;
        // The menu presses start once the menu has drawn 30 frames (half a second of it
        // running), not at a fixed time: loading takes 1 s on the Air X, 10 s on an R36S.
        if (menuReadyAt < 0.0 && gta2_port_present_count() >= 30)
        {
            menuReadyAt = now;
        }
        if (menuReadyAt >= 0.0)
        {
            for (Tap& t : taps)
            {
                if (t.stage == 0 && now >= menuReadyAt + t.at)
                {
                    Gta2Native_AutostartKey(t.which, true);
                    t.stage = 1;
                }
                else if (t.stage == 1 && now >= menuReadyAt + t.at + 100)
                {
                    Gta2Native_AutostartKey(t.which, false);
                    t.stage = 2;
                }
            }
        }

        const double tEvents = emscripten_get_now();
#if !defined(GTA2_NATIVE_HEADLESS)
        if (!Gta2Native_PumpEvents())
        {
            fprintf(stderr, "gta2: asked to quit (window closed or signal)\n");
            break;
        }
#endif
        if (soak)
        {
            Gta2Native_SoakStep(now);
        }
        Gta2Native_ScenarioStep(now);

        const double tAudio = emscripten_get_now();
        Gta2Native_AudioPump(); // streams decoded ahead of the audio callback
        const double tGame = emscripten_get_now();

        const unsigned before = gta2_port_present_count();
        const int slice = Gta2Port_RunLoopSlice();
        if (logStalls)
        {
            const double end = emscripten_get_now();
            if (end - loopStart > 100.0)
            {
                fprintf(stderr, "gta2: slow loop: %.0f ms (before events %.0f, events and test drivers %.0f, audio %.0f, game %.0f)\n",
                        end - loopStart, tEvents - loopStart, tAudio - tEvents, tGame - tAudio, end - tGame);
            }
        }
        if (slice != -1)
        {
            code = slice;
            fprintf(stderr, "gta2: game ended (%d)\n", slice);
            break;
        }
        if (now - lastRegistrySave >= 5000)
        {
            lastRegistrySave = now;
            Gta2Native_RegistrySave(); // only writes when something changed
        }
        if (statusMs > 0 && now - (lastReport - start) >= statusMs)
        {
            const double elapsed = now - (lastReport - start);
            lastReport = start + now;
            int peds = 0;
            int cars = 0;
            const int ticks = Gta2Native_TickCount(&peds, &cars);
            char status[256];
            Gta2Native_StatusLine(status, sizeof(status));
            fprintf(stderr, "gta2: %.0f s, %.1f ticks/s, %u frames, %s\n", now / 1000.0,
                    (ticks - lastTicks) * 1000.0 / elapsed, gta2_port_present_count(), status);
            lastTicks = ticks;
        }
        if (runMs > 0 && now >= runMs)
        {
            fprintf(stderr, "gta2: stopping after %.0f s (GTA2_RUN_SECONDS)\n", now / 1000.0);
            if (soak)
            {
                char summary[128];
                Gta2Native_SoakSummary(summary, sizeof(summary));
                fprintf(stderr, "gta2: %s\n", summary);
            }
            break;
        }
        if (gta2_port_present_count() == before)
        {
            SleepMs(1); // the slice ended on its time budget: nothing to show yet
        }
    }
    Gta2Native_RegistrySave();
    Gta2Native_AudioShutdown();
#if !defined(GTA2_NATIVE_HEADLESS)
    Gta2Native_WindowShutdown();
#endif
    return code;
}
