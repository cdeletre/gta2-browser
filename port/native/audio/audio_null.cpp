// Audio bridges for GTA2_HEADLESS builds (soak tests without a window or sound device):
// nothing is played. Signatures: see port/native/platform/js_bridge_null.cpp.
#if defined(GTA2_NATIVE_HEADLESS)
#include <unistd.h>

extern "C" {

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

void Gta2Native_AudioInit()
{
}
void Gta2Native_AudioPump()
{
}
void Gta2Native_AudioShutdown()
{
}

} // extern "C"
#endif
