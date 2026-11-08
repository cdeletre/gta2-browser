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
// The window and the buffer swap are in port/native/platform/window_sdl.cpp; a GL ES 2.0
// context is current on this thread before the first call here.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <math.h>

#include <unordered_map>

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
varying vec2 vUv;
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

struct Renderer
{
    bool ready = false;
    GLuint program = 0;
    GLuint vbo = 0;
    GLint uView = -1;
    GLint uBlend = -1;
    int blend = -1;
    std::unordered_map<uint32_t, GLuint> textures;
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
} // namespace

extern "C" {

void gta2_js_upload_texture(int id, int w, int h, const void* rgba)
{
    Init();
    GLuint& tex = gR.textures[static_cast<uint32_t>(id)];
    if (!tex)
    {
        glGenTextures(1, &tex);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void gta2_js_free_texture(int id)
{
    const auto it = gR.textures.find(static_cast<uint32_t>(id));
    if (it != gR.textures.end())
    {
        glDeleteTextures(1, &it->second);
        gR.textures.erase(it);
    }
}

void gta2_js_free_all_textures()
{
    for (auto& kv : gR.textures)
    {
        glDeleteTextures(1, &kv.second);
    }
    gR.textures.clear();
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
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertCount) * sizeof(FrameVertex), vertData, GL_STREAM_DRAW);
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

    const FrameCommand* cmds = static_cast<const FrameCommand*>(cmdData);
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
        glBindTexture(GL_TEXTURE_2D, it->second);
        SetBlend(static_cast<int>(c.blend));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(c.first), static_cast<GLsizei>(c.count));
    }
    Gta2Native_EndFrame();
}

} // extern "C"
#endif
