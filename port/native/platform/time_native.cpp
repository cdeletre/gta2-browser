// Native clock behind emscripten_get_now() (see port/native/compat/emscripten.h), which
// the shared port files use for gta2_port_now_ms and frame timing.
#include <time.h>

#include <emscripten.h>

extern "C" double emscripten_get_now(void)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
}
