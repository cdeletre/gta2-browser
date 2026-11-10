// SDL2 window and OpenGL ES 2.0 context for the native build (jalon 3).
//
// Environment:
//   GTA2_WINDOW=WxH          windowed at that size (default: full screen at the desktop size)
//   GTA2_VSYNC=0             no vsync (default 1)
//   GTA2_SCREENSHOT_AT=a,b   save the frame presented at a, b... seconds after start as PNG
//   GTA2_SCREENSHOT_DIR=dir  where (default: the current directory, i.e. the game folder)
// SDL_VIDEODRIVER picks the SDL driver as usual (wayland or kmsdrm on the console,
// offscreen in the build container).
#if !defined(GTA2_NATIVE_HEADLESS)
#include <GLES2/gl2.h>
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include <emscripten.h> // emscripten_get_now (port/native/compat)

// port_prefix.h (forced into every file) routes fopen/fclose to the game's versions, which
// lowercase and resolve game paths; screenshots go to their own absolute paths.
#undef fopen
#undef fclose

extern "C" void Gta2Native_InputInit(); // input_sdl.cpp
extern "C" void Gta2Native_InputEvent(const SDL_Event& e);
extern "C" void Gta2Native_InputFrame();

namespace
{
SDL_Window* gWindow = nullptr;
SDL_GLContext gContext = nullptr;
double gStartMs = 0;
std::vector<double> gShotTimes; // seconds, ascending
size_t gNextShot = 0;
std::string gShotDir = ".";

// --- PNG (uncompressed: stored deflate blocks), enough for test screenshots ---------
uint32_t Crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
    static uint32_t table[256];
    if (!table[1])
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k)
            {
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }
            table[i] = v;
        }
    }
    for (size_t i = 0; i < n; ++i)
    {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c;
}

void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    Put32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    Put32(out, Crc32(&out[start], out.size() - start) ^ 0xFFFFFFFFu);
}

bool WritePng(const char* path, int w, int h, const std::vector<uint8_t>& rgbTopDown)
{
    std::vector<uint8_t> raw; // filter byte 0 + RGB per row
    raw.reserve(static_cast<size_t>(h) * (1 + 3 * w));
    for (int y = 0; y < h; ++y)
    {
        raw.push_back(0);
        raw.insert(raw.end(), rgbTopDown.begin() + static_cast<long>(y) * 3 * w,
                   rgbTopDown.begin() + static_cast<long>(y + 1) * 3 * w);
    }
    std::vector<uint8_t> z = {0x78, 0x01}; // zlib header, stored blocks
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw.size();)
    {
        const size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        for (size_t i = 0; i < n; ++i)
        {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        z.insert(z.end(), raw.begin() + static_cast<long>(off), raw.begin() + static_cast<long>(off + n));
        off += n;
    }
    Put32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    Put32(ihdr, static_cast<uint32_t>(w));
    Put32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        return false;
    }
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

void TakeScreenshot(double seconds)
{
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(gWindow, &w, &h);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) // GL rows are bottom-up
    {
        const uint8_t* src = &rgba[static_cast<size_t>(h - 1 - y) * w * 4];
        uint8_t* dst = &rgb[static_cast<size_t>(y) * w * 3];
        for (int x = 0; x < w; ++x)
        {
            dst[3 * x] = src[4 * x];
            dst[3 * x + 1] = src[4 * x + 1];
            dst[3 * x + 2] = src[4 * x + 2];
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/shot-%03.0fs.png", gShotDir.c_str(), seconds);
    fprintf(stderr, WritePng(path, w, h, rgb) ? "gta2: screenshot %s (%dx%d)\n" : "gta2: cannot write %s\n", path, w, h);
}

void ParseShots()
{
    const char* at = getenv("GTA2_SCREENSHOT_AT");
    for (const char* p = at; p && *p;)
    {
        char* end = nullptr;
        const double s = strtod(p, &end);
        if (end == p)
        {
            break;
        }
        gShotTimes.push_back(s);
        p = *end == ',' ? end + 1 : end;
    }
    if (const char* dir = getenv("GTA2_SCREENSHOT_DIR"))
    {
        gShotDir = dir;
    }
}
} // namespace

extern "C" {

// Before WinMain: the game uploads textures while it creates the front end.
bool Gta2Native_WindowInit()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "gta2: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    int w = 0;
    int h = 0;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    const char* size = getenv("GTA2_WINDOW");
    if (size && sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
    {
        // windowed
    }
    else
    {
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
        {
            w = mode.w;
            h = mode.h;
        }
        else
        {
            w = 640;
            h = 480;
        }
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    gWindow = SDL_CreateWindow("GTA2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!gWindow)
    {
        fprintf(stderr, "gta2: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    gContext = SDL_GL_CreateContext(gWindow);
    if (!gContext || SDL_GL_MakeCurrent(gWindow, gContext) != 0)
    {
        fprintf(stderr, "gta2: no OpenGL ES 2.0 context: %s\n", SDL_GetError());
        return false;
    }
    const char* vsync = getenv("GTA2_VSYNC");
    SDL_GL_SetSwapInterval(vsync && atoi(vsync) == 0 ? 0 : 1);
    SDL_ShowCursor(SDL_DISABLE);
    int dw = 0;
    int dh = 0;
    SDL_GL_GetDrawableSize(gWindow, &dw, &dh);
    fprintf(stderr, "gta2: SDL %s video driver, window %dx%d, drawable %dx%d, swap interval %d\n",
            SDL_GetCurrentVideoDriver(), w, h, dw, dh, SDL_GL_GetSwapInterval());
    ParseShots();
    Gta2Native_InputInit();
    gStartMs = emscripten_get_now();
    return true;
}

void Gta2Native_DrawableSize(int* w, int* h)
{
    SDL_GL_GetDrawableSize(gWindow, w, h);
}

static double gSwapMs = 0;

// After the renderer has drawn a frame (gles_renderer.cpp).
void Gta2Native_EndFrame()
{
    const double seconds = (emscripten_get_now() - gStartMs) / 1000.0;
    if (gNextShot < gShotTimes.size() && seconds >= gShotTimes[gNextShot])
    {
        TakeScreenshot(gShotTimes[gNextShot]);
        while (gNextShot < gShotTimes.size() && seconds >= gShotTimes[gNextShot])
        {
            ++gNextShot;
        }
    }
    const double before = emscripten_get_now();
    SDL_GL_SwapWindow(gWindow);
    gSwapMs += emscripten_get_now() - before;
}

// The time spent in SDL_GL_SwapWindow so far, waiting for vsync mostly (net/net_stats.cpp).
extern "C" double Gta2Native_SwapMsTotal()
{
    return gSwapMs;
}

// Once per loop iteration. Returns false when the window is closed, or when the game is
// asked to stop (SIGTERM or SIGINT, which SDL turns into SDL_QUIT; PortMaster's gptokeyb
// ends the game that way).
bool Gta2Native_PumpEvents()
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            return false;
        }
        Gta2Native_InputEvent(e);
    }
    Gta2Native_InputFrame();
    return true;
}

void Gta2Native_WindowShutdown()
{
    if (gContext)
    {
        SDL_GL_DeleteContext(gContext);
    }
    if (gWindow)
    {
        SDL_DestroyWindow(gWindow);
    }
    SDL_Quit();
}

} // extern "C"
#endif
