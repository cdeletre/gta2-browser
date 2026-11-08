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

#include <math.h>

#include <unordered_map>
#include <vector>

#include "render/frame.h"

extern "C" void Gta2Native_DrawableSize(int* w, int* h); // window_sdl.cpp
extern "C" void Gta2Native_EndFrame();                    // window_sdl.cpp: screenshot, swap

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

struct Page
{
    GLuint tex = 0;
    int bottom = 0; // first row below the shelves
    std::vector<Shelf> shelves;
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
    std::vector<FrameVertex> verts;                             // present scratch
};
Renderer gR;

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
    gR.ready = true;
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

// A free rectangle of (w + 2) x (h + 2) in some page: best-fitting shelf, else a new
// shelf, else a new page.
Slot Allocate(int w, int h)
{
    const auto f = gR.freeSlots.find(static_cast<uint32_t>(w) << 16 | static_cast<uint32_t>(h));
    if (f != gR.freeSlots.end() && !f->second.empty())
    {
        const Slot s = f->second.back();
        f->second.pop_back();
        return s;
    }
    const int pw = w + 2;
    const int ph = h + 2;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t p = 0; p < gR.pages.size(); ++p)
        {
            Page& page = gR.pages[p];
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
                return s;
            }
        }
        Page page;
        glGenTextures(1, &page.tex);
        glBindTexture(GL_TEXTURE_2D, page.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kPage, kPage, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        SetParams();
        gR.pages.push_back(page);
    }
    abort(); // a new page always has room
}

void Release(const Slot& s)
{
    if (s.page < 0)
    {
        glDeleteTextures(1, &s.tex);
    }
    else
    {
        gR.freeSlots[static_cast<uint32_t>(s.w) << 16 | static_cast<uint32_t>(s.h)].push_back(s);
    }
}

// The image with a copy of its edge rows and columns around it.
void UploadPadded(const Slot& s, const uint8_t* rgba)
{
    const int pw = s.w + 2;
    const int ph = s.h + 2;
    gR.padded.resize(static_cast<size_t>(pw) * ph * 4);
    for (int y = 0; y < ph; ++y)
    {
        const int sy = y == 0 ? 0 : y > s.h ? s.h - 1 : y - 1;
        const uint8_t* src = rgba + static_cast<size_t>(sy) * s.w * 4;
        uint8_t* dst = gR.padded.data() + static_cast<size_t>(y) * pw * 4;
        memcpy(dst, src, 4);
        memcpy(dst + 4, src, static_cast<size_t>(s.w) * 4);
        memcpy(dst + static_cast<size_t>(pw - 1) * 4, src + static_cast<size_t>(s.w - 1) * 4, 4);
    }
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, s.x - 1, s.y - 1, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, gR.padded.data());
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
    if (atlas)
    {
        UploadPadded(slot, static_cast<const uint8_t*>(rgba));
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, slot.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        SetParams();
    }
    if (logUploads)
    {
        timespec t1 = {};
        clock_gettime(CLOCK_MONOTONIC, &t1);
        seconds += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
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
    Gta2Native_EndFrame();
}

} // extern "C"
#endif
