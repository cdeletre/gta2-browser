/* Native stand-in for <emscripten.h>, so the shared port files (port/platform,
 * port/render, port/audio) compile unchanged in the native build.
 *
 * - EMSCRIPTEN_KEEPALIVE keeps the symbol (the shell's entry points stay callable).
 * - EM_JS(ret, name, params, body) becomes a plain declaration: the JavaScript body is
 *   dropped and port/native provides a C++ implementation of `name` (a no-op in the
 *   headless build, GLES/SDL later).
 * - EM_ASM* are dropped. The two in win32_shim.cpp only notify the web shell (message
 *   box, cursor); the message box text is already written to stderr before it.
 * - emscripten_get_now() is implemented in port/native/platform/time_native.cpp.
 */
#pragma once

#define EMSCRIPTEN_KEEPALIVE __attribute__((used))

#ifdef __cplusplus
#define GTA2_NATIVE_EXTERN_C extern "C"
#else
#define GTA2_NATIVE_EXTERN_C extern
#endif

#define EM_JS(ret, name, params, ...) GTA2_NATIVE_EXTERN_C ret name params;

#define EM_ASM(...) ((void)0)
#define EM_ASM_INT(...) 0
#define EM_ASM_DOUBLE(...) 0.0

#ifdef __cplusplus
extern "C" {
#endif
/* Milliseconds since an arbitrary start, with sub-millisecond precision (monotonic). */
double emscripten_get_now(void);
#ifdef __cplusplus
}
#endif
