// OpenGL ES 2.0 renderer for the native build: a translation of web/renderer.js (WebGL2).
//
// The core produces one draw list per presented frame (port/render/frame.h): 24-byte
// vertices in the game's reference space (480 high, 640 or the widescreen view width
// across) and 40-byte commands (triangle batches or clears). They are drawn in order,
// with no depth buffer, with the original driver's texture * diffuse modulation and its
// three blend modes. Textures come in as RGBA from port/render/gbh_web.cpp (content-
// addressed ids, see there).
//
// Differences from the WebGL2 version, all forced by GLES 2.0: GLSL #version 100, no
// vertex array object (the attributes are set up at each present), GL_RGBA as the internal
// format. Textures are NPOT with CLAMP_TO_EDGE, NEAREST and no mipmaps, which GLES 2.0
// allows.
//
// Texture atlas (native only): the game registers tens of thousands of small textures (one
// per sprite and palette, about 20x20). Each GL texture costs about 1 ms and 20 KB in the
// Mali blob of an RK3326 (R36S: 80 s and 800 MB to load Downtown), so images up to 254
// pixels a side go into 1024x1024 pages instead, on shelves, with a 1-pixel border copied
// from their edges (sampling at u = 0 or 1 then gives the edge texel, as CLAMP_TO_EDGE on
// their own texture did). Their UVs are remapped at present, and consecutive batches on
// the same page and blend mode become one draw.
// Each small upload into a page still costs about 0.28 ms with Panfrost (TrimUI Smart Pro S:
// 15 s of the 21 s to reach Downtown), so recent pages keep a copy in memory (up to
// kMaxShadows of them): images are written there, and the changed rows go to the GPU in one
// call per page before the next frame is drawn (GLES 2.0 only uploads whole rows). A copy
// is dropped after kShadowIdle frames without writes, or when a newer page needs it; later
// writes to that page are uploaded directly.
// With other drivers small uploads are cheap and whole-page uploads cost more than they
// save (RG CubeXX, Mali blob: 0.04 ms an upload, Downtown 8 s direct, 17 s batched), so the
// first kCalibrationUploads are made directly and timed: batching starts only if their
// median is above kBatchAboveMs (the median, as the first upload into a new page can take
// far longer on some drivers). Medians measured: 0.024 to 0.050 ms on freedreno and the
// Mali blobs of the RG CubeXX and R36S (direct is faster or close), 0.15 to 0.22 ms on the
// Mali-G52 blob of the RG353VS (Downtown 18 s direct, 6 s batched) and 0.22 to 0.28 ms with
// Panfrost; the threshold sits between the two groups. GTA2_ATLAS_BATCH=0 or 1 forces the
// choice.
//
// The window and the buffer swap are in port/native/platform/window_sdl.cpp; a GL ES 2.0
// context is current on this thread before the first call here.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ctype.h>
#include <math.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "render/frame.h"
#include "overlay_font.h"

extern "C" void Gta2Native_DrawableSize(int* w, int* h); // window_sdl.cpp
extern "C" void Gta2Native_EndFrame();                    // window_sdl.cpp: screenshot, swap
extern "C" void Gta2Native_OptionsDraw(int dw, int dh);   // platform/options_native.cpp

namespace
{
int gRefH = 480; // frame height in reference pixels: 480, more in the tall view (frame.cpp)
static_assert(sizeof(FrameVertex) == 24, "web/renderer.js layout");
static_assert(sizeof(FrameCommand) == 40, "web/renderer.js layout");

const char* const kVertexShader = R"(#version 100
attribute vec2 aPos;    // reference-space pixels
attribute float aRhw;   // reciprocal W from the game
attribute vec2 aUv;
attribute vec4 aColour;
uniform vec4 uView;     // xy: scale, zw: offset (reference px -> NDC)
varying vec2 vUv;
varying vec4 vColour;
void main() {
  // Clip-space W = 1/rhw gives perspective-correct texturing, as the original
  // pre-transformed vertices did.
  float w = 1.0 / max(aRhw, 1e-6);
  vec2 ndc = aPos * uView.xy + uView.zw;
  gl_Position = vec4(ndc * w, 0.0, w);
  vUv = aUv;
  vColour = aColour;
}
)";

const char* const kFragmentShader = R"(#version 100
precision mediump float;
// Atlas UVs need a texel in 1024: more than mediump's 10-bit mantissa.
#ifdef GL_FRAGMENT_PRECISION_HIGH
varying highp vec2 vUv;
#else
varying vec2 vUv;
#endif
varying vec4 vColour;
uniform sampler2D uTexture;
uniform int uBlend;
void main() {
  vec4 t = texture2D(uTexture, vUv);
  if (t.a < 0.5) discard;              // palette colour 0 is transparent
  vec3 rgb = t.rgb * vColour.rgb;      // texture * diffuse
  float a = uBlend == 1 ? t.a * vColour.a : 1.0;
  gl_FragColor = vec4(rgb, a);
}
)";

enum Attrib : GLuint
{
    kPos = 0,
    kRhw = 1,
    kUv = 2,
    kColour = 3
};

constexpr int kPage = 1024;   // atlas page size
constexpr int kMaxInAtlas = 254; // larger images (plus border) get their own texture

struct Shelf
{
    int y, h, x;
};

constexpr int kMaxShadows = 8;      // pages with a copy in memory (2 MB each in 16-bit, 4 in 32)
constexpr unsigned kCalibrationUploads = 1000;
constexpr double kBatchAboveMs = 0.10;
constexpr unsigned kShadowIdle = 300; // frames without writes before a copy is dropped

struct Page
{
    GLuint tex = 0;
    int bottom = 0; // first row below the shelves
    std::vector<Shelf> shelves;
    std::vector<uint8_t> shadow; // the page's pixels, while it has a copy
    int dirtyTop = kPage;        // rows changed in the copy since the last upload
    int dirtyBottom = -1;
    unsigned lastWrite = 0;      // frame of the last write
    int live = 0;                // textures in the page; at 0 it starts again empty
};

// Where a game texture is: its own GL texture (page < 0), or a rectangle of an atlas page
// (x, y: the image's top-left inside the border).
struct Slot
{
    GLuint tex = 0;
    int page = -1;
    int x = 0, y = 0, w = 0, h = 0;
};

struct Renderer
{
    bool ready = false;
    GLuint program = 0;
    GLuint vbo = 0;
    GLint uView = -1;
    GLint uBlend = -1;
    int blend = -1;
    std::unordered_map<uint32_t, Slot> textures;
    std::vector<Page> pages;
    std::unordered_map<uint32_t, std::vector<Slot>> freeSlots; // by (w << 16 | h)
    std::vector<uint8_t> padded;                                // upload scratch
    std::vector<uint8_t> packed;                                // the image in the texture format
    int bpp = 2;                                                // bytes a texel: 2 (RGB5_A1) or 4
    GLenum texType = GL_UNSIGNED_SHORT_5_5_5_1;
    std::vector<FrameVertex> verts;                             // present scratch
    unsigned frame = 0;                                         // presents so far
    int shadows = 0;                                            // pages with a copy
    int batch = -1;                                             // batched page uploads: -1 not decided yet
    std::vector<float> timedMs;                                 // calibration samples
    GLuint fontTex = 0;                                         // overlay_font.h glyphs
    unsigned char glyphIndex[128] = {};                         // character -> glyph
    std::vector<FrameVertex> overlay;                           // the port's own screens
};
Renderer gR;

// GTA2_LOG_TEXTURES=1 also logs slow frames: more than 100 ms between two presents, with the
// texture uploads made in between and the time spent in the buffer swap.
struct FrameStats
{
    unsigned uploads = 0;
    double uploadSeconds = 0.0;
    double flushSeconds = 0.0; // page uploads (FlushPages)
    double lastPresent = 0.0;
};
FrameStats gStats;

double NowSeconds()
{
    timespec t = {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

GLuint Compile(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "gta2: shader compile failed: %s\n", log);
        abort();
    }
    return shader;
}

void SetParams();
void SetBlend(int mode);

void Init()
{
    if (gR.ready)
    {
        return;
    }
    gR.program = glCreateProgram();
    glAttachShader(gR.program, Compile(GL_VERTEX_SHADER, kVertexShader));
    glAttachShader(gR.program, Compile(GL_FRAGMENT_SHADER, kFragmentShader));
    glBindAttribLocation(gR.program, kPos, "aPos");
    glBindAttribLocation(gR.program, kRhw, "aRhw");
    glBindAttribLocation(gR.program, kUv, "aUv");
    glBindAttribLocation(gR.program, kColour, "aColour");
    glLinkProgram(gR.program);
    GLint ok = 0;
    glGetProgramiv(gR.program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(gR.program, sizeof(log), nullptr, log);
        fprintf(stderr, "gta2: shader link failed: %s\n", log);
        abort();
    }
    gR.uView = glGetUniformLocation(gR.program, "uView");
    gR.uBlend = glGetUniformLocation(gR.program, "uBlend");
    glUseProgram(gR.program);
    glUniform1i(glGetUniformLocation(gR.program, "uTexture"), 0);
    glGenBuffers(1, &gR.vbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    fprintf(stderr, "gta2: GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    // Textures are 16-bit RGB5_A1: half the memory of RGBA8 (with the Mali blob and Panfrost
    // that memory is the console's RAM), and the palette colour 0's transparency is exactly the
    // 1-bit alpha. GTA2_TEXTURE_BITS=32 keeps RGBA8.
    if (const char* bits = getenv("GTA2_TEXTURE_BITS"); bits && atoi(bits) == 32)
    {
        gR.bpp = 4;
        gR.texType = GL_UNSIGNED_BYTE;
    }
    fprintf(stderr, "gta2: textures: %s\n", gR.bpp == 2 ? "16-bit (RGB5_A1)" : "32-bit (RGBA8, GTA2_TEXTURE_BITS)");
    if (const char* batch = getenv("GTA2_ATLAS_BATCH"))
    {
        gR.batch = atoi(batch) != 0 ? 1 : 0;
        fprintf(stderr, "gta2: atlas: %s (GTA2_ATLAS_BATCH)\n", gR.batch ? "batched page uploads" : "direct uploads");
    }
    // The overlay font: glyphs in 8x8 cells, 16 to a row, white where lit.
    {
        constexpr int kFontW = 128, kFontH = 64;
        std::vector<uint8_t> px(kFontW * kFontH * 4, 0);
        const int count = static_cast<int>(sizeof(kOverlayFont) / sizeof(kOverlayFont[0]));
        for (int i = 0; i < count; ++i)
        {
            gR.glyphIndex[static_cast<unsigned char>(kOverlayFont[i].c) & 127] = static_cast<unsigned char>(i);
            const int cx = (i % 16) * 8, cy = (i / 16) * 8;
            for (int y = 0; y < kGlyphH; ++y)
            {
                for (int x = 0; x < kGlyphW; ++x)
                {
                    if (kOverlayFont[i].rows[y][x] == '1')
                    {
                        memset(&px[((cy + y) * kFontW + cx + x) * 4], 255, 4);
                    }
                }
            }
        }
        glGenTextures(1, &gR.fontTex);
        glBindTexture(GL_TEXTURE_2D, gR.fontTex);
        SetParams();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFontW, kFontH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    gR.ready = true;
}

// Overlay quads, in drawable pixels (top left origin), font texture coordinates.
void OverlayQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba)
{
    const FrameVertex a{x, y, 1, u0, v0, rgba}, b{x + w, y, 1, u1, v0, rgba};
    const FrameVertex c{x, y + h, 1, u0, v1, rgba}, d{x + w, y + h, 1, u1, v1, rgba};
    gR.overlay.insert(gR.overlay.end(), {a, b, c, b, d, c});
}

void DrawOverlay(int dw, int dh)
{
    gR.overlay.clear();
    Gta2Native_OptionsDraw(dw, dh);
    if (gR.overlay.empty())
    {
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, dw, dh);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gR.overlay.size()) * sizeof(FrameVertex), gR.overlay.data(),
                 GL_STREAM_DRAW);
    glVertexAttribPointer(kPos, 2, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(0));
    glVertexAttribPointer(kRhw, 1, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(8));
    glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(12));
    glVertexAttribPointer(kColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FrameVertex), reinterpret_cast<const void*>(20));
    glUniform4f(gR.uView, 2.0f / dw, -2.0f / dh, -1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, gR.fontTex);
    SetBlend(kBlendAlpha);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(gR.overlay.size()));
}

void SetBlend(int mode)
{
    if (mode == gR.blend)
    {
        return;
    }
    gR.blend = mode;
    glUniform1i(gR.uBlend, mode);
    if (mode == kBlendOpaque)
    {
        glDisable(GL_BLEND);
    }
    else if (mode == kBlendAlpha)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    }
}
void SetParams()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Uploads the rows of a page's copy changed since the last upload.
void FlushPage(Page& page)
{
    if (page.dirtyBottom < page.dirtyTop)
    {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, page.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, page.dirtyTop, kPage, page.dirtyBottom - page.dirtyTop + 1, GL_RGBA,
                    gR.texType, page.shadow.data() + static_cast<size_t>(page.dirtyTop) * kPage * gR.bpp);
    page.dirtyTop = kPage;
    page.dirtyBottom = -1;
}

void DropShadow(Page& page)
{
    FlushPage(page);
    std::vector<uint8_t>().swap(page.shadow);
    --gR.shadows;
}

// A new page starts with a copy in memory (all zero, as the texture), at the cost of the
// least recently written page's copy when kMaxShadows are in use.
void GiveShadow(size_t index)
{
    if (gR.shadows >= kMaxShadows)
    {
        Page* oldest = nullptr;
        for (Page& page : gR.pages)
        {
            if (!page.shadow.empty() && (!oldest || page.lastWrite < oldest->lastWrite))
            {
                oldest = &page;
            }
        }
        if (oldest)
        {
            DropShadow(*oldest);
        }
    }
    gR.pages[index].shadow.assign(static_cast<size_t>(kPage) * kPage * gR.bpp, 0);
    gR.pages[index].lastWrite = gR.frame;
    ++gR.shadows;
}

// Before a frame is drawn: the changed rows of every copy go to the GPU, and copies left
// alone for kShadowIdle frames are dropped.
void FlushPages()
{
    for (Page& page : gR.pages)
    {
        if (page.shadow.empty())
        {
            continue;
        }
        FlushPage(page);
        if (gR.frame - page.lastWrite > kShadowIdle)
        {
            DropShadow(page);
        }
    }
}

// A free rectangle of (w + 2) x (h + 2) in some page: best-fitting shelf, else a new
// shelf, else a new page.
Slot Allocate(int w, int h)
{
    const auto f = gR.freeSlots.find(static_cast<uint32_t>(w) << 16 | static_cast<uint32_t>(h));
    if (f != gR.freeSlots.end() && !f->second.empty())
    {
        const Slot s = f->second.back();
        f->second.pop_back();
        ++gR.pages[s.page].live;
        return s;
    }
    const int pw = w + 2;
    const int ph = h + 2;
    for (int pass = 0; pass < 4; ++pass)
    {
        for (size_t p = 0; p < gR.pages.size(); ++p)
        {
            Page& page = gR.pages[p];
            // Pass 0: pages with a copy in memory; pass 1: empty pages (emptied by freed
            // textures, Release), which take a copy as a new page does when uploads are
            // batched; pass 2: any page; pass 3 after a new page.
            if (pass == 0 && page.shadow.empty())
            {
                continue;
            }
            if (pass == 1)
            {
                if (page.live != 0 || page.bottom != 0)
                {
                    continue;
                }
                if (gR.batch == 1 && page.shadow.empty())
                {
                    GiveShadow(p); // empty: a fresh, zeroed copy loses nothing
                }
            }
            Shelf* best = nullptr;
            for (Shelf& sh : page.shelves)
            {
                if (sh.h >= ph && sh.h <= ph + ph / 4 + 2 && sh.x + pw <= kPage && (!best || sh.h < best->h))
                {
                    best = &sh;
                }
            }
            if (!best && page.bottom + ph <= kPage)
            {
                page.shelves.push_back({page.bottom, ph, 0});
                page.bottom += ph;
                best = &page.shelves.back();
            }
            if (best)
            {
                Slot s;
                s.tex = page.tex;
                s.page = static_cast<int>(p);
                s.x = best->x + 1;
                s.y = best->y + 1;
                s.w = w;
                s.h = h;
                best->x += pw;
                ++page.live;
                return s;
            }
        }
        if (pass == 2)
        {
            Page page;
            glGenTextures(1, &page.tex);
            glBindTexture(GL_TEXTURE_2D, page.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kPage, kPage, 0, GL_RGBA, gR.texType, nullptr);
            SetParams();
            gR.pages.push_back(std::move(page));
            if (gR.batch == 1)
            {
                GiveShadow(gR.pages.size() - 1);
            }
        }
    }
    abort(); // a new page always has room
}

void Release(const Slot& s)
{
    if (s.page < 0)
    {
        glDeleteTextures(1, &s.tex);
    }
    else if (--gR.pages[s.page].live == 0)
    {
        // The page holds no texture any more (the game freed them, render/gbh_web.cpp): its
        // shelves start again, and the free rectangles listed for it go. Allocate fills empty
        // pages before partly full ones and, with batched uploads, gives them a copy in memory
        // as it does a new page: without that every load after the first uploaded directly
        // (TrimUI Smart Pro S, Panfrost: 37 000 textures in 10 s instead of 1.5 s). Only an
        // empty page can take a fresh, zeroed copy: GLES 2 cannot read a texture back.
        Page& page = gR.pages[s.page];
        page.shelves.clear();
        page.bottom = 0;
        for (auto& kv : gR.freeSlots)
        {
            auto& v = kv.second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Slot& f) { return f.page == s.page; }), v.end());
        }
    }
    else
    {
        gR.freeSlots[static_cast<uint32_t>(s.w) << 16 | static_cast<uint32_t>(s.h)].push_back(s);
    }
}

// RGBA8 pixels in the texture format (gR.bpp bytes each).
const uint8_t* Pack(const uint8_t* rgba, int w, int h)
{
    if (gR.bpp == 4)
    {
        return rgba;
    }
    const size_t n = static_cast<size_t>(w) * h;
    gR.packed.resize(n * 2);
    uint16_t* out = reinterpret_cast<uint16_t*>(gR.packed.data());
    for (size_t i = 0; i < n; ++i, rgba += 4)
    {
        const unsigned r = (rgba[0] * 31u + 127u) / 255u, g = (rgba[1] * 31u + 127u) / 255u;
        const unsigned b = (rgba[2] * 31u + 127u) / 255u;
        out[i] = static_cast<uint16_t>(r << 11 | g << 6 | b << 1 | (rgba[3] >= 128 ? 1u : 0u));
    }
    return gR.packed.data();
}

// The image (texture format) with a copy of its edge rows and columns around it.
void UploadPadded(const Slot& s, const uint8_t* texels)
{
    const int pw = s.w + 2;
    const int ph = s.h + 2;
    const size_t bpp = static_cast<size_t>(gR.bpp);
    gR.padded.resize(static_cast<size_t>(pw) * ph * bpp);
    for (int y = 0; y < ph; ++y)
    {
        const int sy = y == 0 ? 0 : y > s.h ? s.h - 1 : y - 1;
        const uint8_t* src = texels + static_cast<size_t>(sy) * s.w * bpp;
        uint8_t* dst = gR.padded.data() + static_cast<size_t>(y) * pw * bpp;
        memcpy(dst, src, bpp);
        memcpy(dst + bpp, src, static_cast<size_t>(s.w) * bpp);
        memcpy(dst + static_cast<size_t>(pw - 1) * bpp, src + static_cast<size_t>(s.w - 1) * bpp, bpp);
    }
    Page& page = gR.pages[static_cast<size_t>(s.page)];
    if (page.shadow.empty())
    {
        const bool timing = gR.batch < 0;
        const double t0 = timing ? NowSeconds() : 0.0;
        glBindTexture(GL_TEXTURE_2D, s.tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, s.x - 1, s.y - 1, pw, ph, GL_RGBA, gR.texType, gR.padded.data());
        if (timing)
        {
            gR.timedMs.push_back(static_cast<float>((NowSeconds() - t0) * 1000.0));
            if (gR.timedMs.size() == kCalibrationUploads)
            {
                std::sort(gR.timedMs.begin(), gR.timedMs.end());
                const auto at = [](unsigned per100) { return gR.timedMs[kCalibrationUploads * per100 / 100]; };
                const double ms = at(50);
                gR.batch = ms > kBatchAboveMs ? 1 : 0;
                fprintf(stderr, "gta2: atlas: median %.3f ms an upload (10%%: %.3f, 25%%: %.3f, 75%%: %.3f, 90%%: %.3f), %s\n",
                        ms, at(10), at(25), at(75), at(90), gR.batch ? "batched page uploads" : "direct uploads");
                std::vector<float>().swap(gR.timedMs);
            }
        }
        return;
    }
    for (int y = 0; y < ph; ++y)
    {
        memcpy(page.shadow.data() + (static_cast<size_t>(s.y - 1 + y) * kPage + (s.x - 1)) * bpp,
               gR.padded.data() + static_cast<size_t>(y) * pw * bpp, static_cast<size_t>(pw) * bpp);
    }
    page.dirtyTop = s.y - 1 < page.dirtyTop ? s.y - 1 : page.dirtyTop;
    page.dirtyBottom = s.y - 2 + ph > page.dirtyBottom ? s.y - 2 + ph : page.dirtyBottom;
    page.lastWrite = gR.frame;
}
} // namespace

extern "C" {

void gta2_js_upload_texture(int id, int w, int h, const void* rgba)
{
    Init();
    // GTA2_LOG_TEXTURES=1: a line every 500 uploads (count, live textures, bytes, time).
    static const bool logUploads = getenv("GTA2_LOG_TEXTURES") && atoi(getenv("GTA2_LOG_TEXTURES")) != 0;
    static unsigned uploads = 0, fresh = 0;
    static double bytes = 0.0, seconds = 0.0;
    timespec t0 = {};
    if (logUploads)
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
    }
    Slot& slot = gR.textures[static_cast<uint32_t>(id)];
    const bool atlas = w > 0 && h > 0 && w <= kMaxInAtlas && h <= kMaxInAtlas;
    if (slot.tex && (slot.w != w || slot.h != h || (slot.page >= 0) != atlas))
    {
        Release(slot);
        slot = Slot();
    }
    if (!slot.tex)
    {
        ++fresh;
        if (atlas)
        {
            slot = Allocate(w, h);
        }
        else
        {
            glGenTextures(1, &slot.tex);
            slot.w = w;
            slot.h = h;
        }
    }
    const uint8_t* texels = Pack(static_cast<const uint8_t*>(rgba), w, h);
    if (atlas)
    {
        UploadPadded(slot, texels);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, slot.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, gR.texType, texels);
        SetParams();
    }
    if (logUploads)
    {
        timespec t1 = {};
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        seconds += dt;
        ++gStats.uploads;
        gStats.uploadSeconds += dt;
        bytes += 4.0 * w * h;
        if (++uploads % 500 == 0)
        {
            fprintf(stderr, "gta2: textures: %u uploads (%u new ids, %zu live, %zu atlas pages), %.1f MB, %.2f s uploading, last %dx%d\n",
                    uploads, fresh, gR.textures.size(), gR.pages.size(), bytes / 1e6, seconds, w, h);
        }
    }
}

void gta2_js_free_texture(int id)
{
    const auto it = gR.textures.find(static_cast<uint32_t>(id));
    if (it != gR.textures.end())
    {
        Release(it->second);
        gR.textures.erase(it);
    }
}

void gta2_js_free_all_textures()
{
    for (auto& kv : gR.textures)
    {
        if (kv.second.page < 0)
        {
            glDeleteTextures(1, &kv.second.tex);
        }
    }
    for (Page& page : gR.pages)
    {
        glDeleteTextures(1, &page.tex);
    }
    gR.textures.clear();
    gR.pages.clear();
    gR.freeSlots.clear();
    gR.shadows = 0;
}

// The port's own screens (platform/options_native.cpp), drawn over the game's frame from
// Gta2Native_OptionsDraw: drawable pixels from the top left, colours as R,G,B,A bytes.
void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba)
{
    const int i = gR.glyphIndex[0x7f];
    const float u = ((i % 16) * 8 + 2.5f) / 128.0f, v = ((i / 16) * 8 + 3.5f) / 64.0f; // inside the block
    OverlayQuad(x, y, w, h, u, v, u, v, rgba);
}

// One line of text, px screen pixels to a font pixel; a character is 6 font pixels wide.
void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text)
{
    for (; *text; ++text, x += 6 * px)
    {
        const int c = toupper(static_cast<unsigned char>(*text)) & 127;
        const int i = gR.glyphIndex[c];
        if (i == 0)
        {
            continue; // space, or no glyph
        }
        const float u0 = ((i % 16) * 8) / 128.0f, v0 = ((i / 16) * 8) / 64.0f;
        OverlayQuad(x, y, kGlyphW * px, kGlyphH * px, u0, v0, u0 + kGlyphW / 128.0f, v0 + kGlyphH / 64.0f, rgba);
    }
}

// The status line (main.cpp): GPU textures alive, atlas pages, and the pixels they hold in MB.
void Gta2Native_TextureStats(int* textures, int* pages, int* mb)
{
    size_t bytes = gR.pages.size() * static_cast<size_t>(kPage) * kPage * gR.bpp;
    for (const auto& kv : gR.textures)
    {
        if (kv.second.page < 0)
        {
            bytes += static_cast<size_t>(kv.second.w) * kv.second.h * gR.bpp;
        }
    }
    *textures = static_cast<int>(gR.textures.size());
    *pages = static_cast<int>(gR.pages.size());
    *mb = static_cast<int>(bytes >> 20);
}

void Gta2Native_SetPresentHeight(int height)
{
    gRefH = height > 0 ? height : 480;
}

void gta2_js_present(const void* cmdData, int cmdCount, const void* vertData, int vertCount, int refW)
{
    Init();
    int dw = 1;
    int dh = 1;
    Gta2Native_DrawableSize(&dw, &dh);
    if (refW <= 0)
    {
        refW = 640;
    }
    // Fit refW x refH into the drawable (letter- or pillarboxed), as renderer.js resize().
    // Gameplay views are cut to the screen's shape (main.cpp); when the shapes differ by
    // less than 2% (rounding to even sizes) the frame is stretched to fill the screen.
    const int refH = gRefH;
    const float sx = static_cast<float>(dw) / refW;
    const float sy = static_cast<float>(dh) / refH;
    const bool fill = fabsf(sx / sy - 1.0f) < 0.02f;
    const float scaleX = fill ? sx : fminf(sx, sy);
    const float scaleY = fill ? sy : scaleX;
    const int vpw = fill ? dw : static_cast<int>(lroundf(refW * scaleX));
    const int vph = fill ? dh : static_cast<int>(lroundf(refH * scaleY));
    const int vpx = (dw - vpw) / 2;
    const int vpy = (dh - vph) / 2;

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vpx, vpy, vpw, vph);
    glUseProgram(gR.program);
    glBindBuffer(GL_ARRAY_BUFFER, gR.vbo);
    ++gR.frame;
    {
        static const bool logFlush = getenv("GTA2_LOG_TEXTURES") && atoi(getenv("GTA2_LOG_TEXTURES")) != 0;
        const double t0 = logFlush ? NowSeconds() : 0.0;
        FlushPages();
        if (logFlush)
        {
            gStats.flushSeconds += NowSeconds() - t0;
        }
    }
    // Atlas UVs: each batch's vertices are moved into its texture's rectangle.
    const FrameCommand* cmds = static_cast<const FrameCommand*>(cmdData);
    gR.verts.assign(static_cast<const FrameVertex*>(vertData), static_cast<const FrameVertex*>(vertData) + vertCount);
    for (int i = 0; i < cmdCount; ++i)
    {
        const FrameCommand& c = cmds[i];
        if (c.kind != 0)
        {
            continue;
        }
        const auto it = gR.textures.find(c.texture);
        if (it == gR.textures.end() || it->second.page < 0)
        {
            continue;
        }
        const Slot& s = it->second;
        const float su = static_cast<float>(s.w) / kPage, ou = static_cast<float>(s.x) / kPage;
        const float sv = static_cast<float>(s.h) / kPage, ov = static_cast<float>(s.y) / kPage;
        for (uint32_t k = c.first; k < c.first + c.count && k < static_cast<uint32_t>(vertCount); ++k)
        {
            gR.verts[k].u = ou + gR.verts[k].u * su;
            gR.verts[k].v = ov + gR.verts[k].v * sv;
        }
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertCount) * sizeof(FrameVertex), gR.verts.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 2, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(kRhw);
    glVertexAttribPointer(kRhw, 1, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(8));
    glEnableVertexAttribArray(kUv);
    glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(FrameVertex), reinterpret_cast<const void*>(12));
    glEnableVertexAttribArray(kColour);
    glVertexAttribPointer(kColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FrameVertex), reinterpret_cast<const void*>(20));
    glUniform4f(gR.uView, 2.0f / refW, -2.0f / refH, -1.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    gR.blend = -1;

    GLuint bound = 0;
    for (int i = 0; i < cmdCount; ++i)
    {
        const FrameCommand& c = cmds[i];
        if (c.kind == 1)
        {
            const int l = c.rect[0], t = c.rect[1], r = c.rect[2], b = c.rect[3];
            glEnable(GL_SCISSOR_TEST);
            if (r > l && b > t)
            {
                glScissor(vpx + static_cast<int>(floorf(l * scaleX)), vpy + static_cast<int>(floorf((refH - b) * scaleY)),
                          static_cast<int>(ceilf((r - l) * scaleX)), static_cast<int>(ceilf((b - t) * scaleY)));
            }
            else
            {
                glScissor(0, 0, dw, dh);
            }
            glClearColor((c.colour & 255) / 255.0f, ((c.colour >> 8) & 255) / 255.0f, ((c.colour >> 16) & 255) / 255.0f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            continue;
        }
        const auto it = gR.textures.find(c.texture);
        if (it == gR.textures.end())
        {
            continue;
        }
        // Following batches drawn from the same GL texture with the same blend, with their
        // vertices right after these, go into the same draw.
        const GLuint tex = it->second.tex;
        uint32_t count = c.count;
        while (i + 1 < cmdCount)
        {
            const FrameCommand& n = cmds[i + 1];
            if (n.kind != 0 || n.blend != c.blend || n.first != c.first + count)
            {
                break;
            }
            const auto nt = gR.textures.find(n.texture);
            if (nt == gR.textures.end() || nt->second.tex != tex)
            {
                break;
            }
            count += n.count;
            ++i;
        }
        if (tex != bound)
        {
            glBindTexture(GL_TEXTURE_2D, tex);
            bound = tex;
        }
        SetBlend(static_cast<int>(c.blend));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(c.first), static_cast<GLsizei>(count));
    }
    DrawOverlay(dw, dh);
    static const bool logFrames = getenv("GTA2_LOG_TEXTURES") && atoi(getenv("GTA2_LOG_TEXTURES")) != 0;
    const double beforeSwap = logFrames ? NowSeconds() : 0.0;
    Gta2Native_EndFrame();
    if (logFrames)
    {
        const double now = NowSeconds();
        if (gStats.lastPresent > 0.0 && now - gStats.lastPresent > 0.1)
        {
            fprintf(stderr, "gta2: slow frame: %.0f ms since the last, %u texture uploads in %.0f ms, page uploads %.0f ms, swap %.0f ms\n",
                    (now - gStats.lastPresent) * 1000.0, gStats.uploads, gStats.uploadSeconds * 1000.0,
                    gStats.flushSeconds * 1000.0, (now - beforeSwap) * 1000.0);
        }
        gStats.lastPresent = now;
        gStats.uploads = 0;
        gStats.uploadSeconds = 0.0;
        gStats.flushSeconds = 0.0;
    }
}

} // extern "C"
#endif
