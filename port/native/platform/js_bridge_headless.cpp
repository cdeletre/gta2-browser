// Headless (jalon 1) implementations of the EM_JS bridges the shared port files call.
// In the browser these reach web/renderer.js, web/audio.js, the save storage and the
// co-op transport; here nothing is drawn, played or sent.
//
// The signatures are copied from the EM_JS declarations (port/native/compat/emscripten.h
// turns each into an extern "C" declaration): C linkage means the linker cannot check
// them, so keep them identical when a bridge changes.
#if defined(GTA2_NATIVE_HEADLESS)
#include <unistd.h>

#include <stdio.h>

extern "C" {

// --- render/frame.cpp, render/gbh_web.cpp --------------------------------------------
void gta2_js_present(const void*, int, const void*, int, int)
{
}
void gta2_js_upload_texture(int, int, int, const void*)
{
}
void gta2_js_free_texture(int)
{
}
void gta2_js_free_all_textures()
{
}

// --- platform/crt_compat.cpp: the shell's save storage (files stay on disk natively) --
void gta2_js_file_written(const char*)
{
}
void gta2_js_file_removed(const char*)
{
}

// --- audio/miles_web.cpp -------------------------------------------------------------
void gta2_js_sample_start(int, const void*, int, int, int, int, int, int, int, int)
{
}
void gta2_js_sample_update(int, int, int, int)
{
}
void gta2_js_sample_stop(int)
{
}
// A stream "opens" when its file is readable, so the game takes the same path as with
// sound, and it ends at once (never playing, position 0).
int gta2_js_stream_open(int, const char* path)
{
    return path && access(path, R_OK) == 0 ? 1 : 0;
}
void gta2_js_stream_close(int)
{
}
void gta2_js_stream_start(int, int)
{
}
int gta2_js_stream_playing(int)
{
    return 0;
}
void gta2_js_stream_set(int, int, int)
{
}
void gta2_js_stream_seek(int, int)
{
}
int gta2_js_stream_ms(int, int)
{
    return 0;
}
void gta2_js_audio_reset()
{
}
void gta2_js_master_volume(int)
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
#endif
