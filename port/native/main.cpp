// Native entry point (Linux aarch64). Jalon 1: headless, no window, no sound.
//
// Does what web/shell.js and port/platform/main_web.cpp do in the browser:
// registry defaults, the original's C runtime start-up (Gta2Port_StaticInit),
// WinMain's initialisation, then the message loop in slices (patch 0007).
//
// Environment:
//   GTA2_DATA_DIR     game folder (the one holding data/); default: the binary's folder
//   GTA2_AUTOSTART=1  press Enter twice like ?autostart=1 (PLAY, then area 1: Downtown)
//   GTA2_RUN_SECONDS  stop cleanly after this many seconds (soak tests)
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>

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
    // The 10.5 code is the freeloader.com edition: 2 unlocks all three districts.
    gta2_port_reg_set_dword("hklm\\software\\freeloader.com\\gta2", "cityinstalled", 2);
    gta2_port_reg_set_string((std::string(kRegRoot) + "\\option").c_str(), "language", "e");
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
};
} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char* env = getenv("GTA2_DATA_DIR");
    const std::string dataDir = env && *env ? env : BinaryDir();
    if (chdir(dataDir.c_str()) != 0)
    {
        fprintf(stderr, "gta2: cannot open the game folder %s\n", dataDir.c_str());
        return 1;
    }
    fprintf(stderr, "gta2: game folder %s\n", dataDir.c_str());
    // The game writes its player data and saves to player\ (the web shell creates it too).
    // TODO (jalon 1, item 4): $GTA2_SAVE_DIR.
    if (mkdir("player", 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "gta2: cannot create %s/player: %s\n", dataDir.c_str(), strerror(errno));
        return 1;
    }

    SeedRegistry();
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
    const bool autostart = getenv("GTA2_AUTOSTART") && atoi(getenv("GTA2_AUTOSTART")) != 0;
    Tap taps[] = {{1500, 0}, {3500, 0}};

    const double start = emscripten_get_now();
    double lastReport = start;
    int code = 0;
    for (;;)
    {
        const double now = emscripten_get_now() - start;
        if (autostart)
        {
            for (Tap& t : taps)
            {
                if (t.stage == 0 && now >= t.at)
                {
                    gta2_port_key(0x1c, 0x0d, 1, 13);
                    t.stage = 1;
                }
                else if (t.stage == 1 && now >= t.at + 100)
                {
                    gta2_port_key(0x1c, 0x0d, 0, 0);
                    t.stage = 2;
                }
            }
        }

        const unsigned before = gta2_port_present_count();
        const int slice = Gta2Port_RunLoopSlice();
        if (slice != -1)
        {
            code = slice;
            fprintf(stderr, "gta2: game ended (%d)\n", slice);
            break;
        }
        if (now - (lastReport - start) >= 5000)
        {
            lastReport = start + now;
            char status[256];
            Gta2Native_StatusLine(status, sizeof(status));
            fprintf(stderr, "gta2: %.0f s, %u frames presented, %s\n", now / 1000.0, gta2_port_present_count(), status);
        }
        if (runMs > 0 && now >= runMs)
        {
            fprintf(stderr, "gta2: stopping after %.0f s (GTA2_RUN_SECONDS)\n", now / 1000.0);
            break;
        }
        if (gta2_port_present_count() == before)
        {
            SleepMs(1); // the slice ended on its time budget: nothing to show yet
        }
    }
    return code;
}
