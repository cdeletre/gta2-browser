// Native implementations of the EM_JS bridges the shared port files call, for the parts
// the native build does not have yet: in the browser these reach web/audio.js, the save
// storage, the co-op transport and the Controls screen; here nothing is sent.
// The render bridges are in port/native/render/ (gles_renderer.cpp, or render_headless.cpp
// for GTA2_HEADLESS builds), the audio ones in port/native/audio/ (audio_sdl.cpp, or
// audio_null.cpp).
//
// The signatures are copied from the EM_JS declarations (port/native/compat/emscripten.h
// turns each into an extern "C" declaration): C linkage means the linker cannot check
// them, so keep them identical when a bridge changes.
#include <unistd.h>

#include <stdio.h>

extern "C" {

// --- platform/crt_compat.cpp: the shell's save storage (files stay on disk natively) --
void gta2_js_file_written(const char*)
{
}
void gta2_js_file_removed(const char*)
{
}

// --- platform/net_web.cpp: no co-op transport natively -------------------------------
void gta2_js_net_send(int, const void*, int)
{
}

// --- platform/options_web.cpp: the front end's Options item ---------------------------
// The browser opens its Controls screen; natively there is none yet, so the menu stays.
void gta2_js_open_options()
{
    fprintf(stderr, "gta2: Options (controls screen) not available in the native build yet\n");
}

} // extern "C"
