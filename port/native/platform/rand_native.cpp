// The C library's rand() and srand() as the browser build has them (Emscripten's musl), in
// place of glibc's. The engine's world random numbers (rng::get_int_4F7AE0 and friends,
// gRng_6F6784) come straight from rand(): with glibc's sequence a native player and a
// browser drew different numbers from the same srand(1), so a co-op match drifted apart
// from the first ped spawned (seen in A3: ped 9 at tick 2, its spawn jitter). The original's
// MSVC rand() is a third sequence (and RAND_MAX 0x7FFF); keeping the browser's keeps both
// ports identical. A definition in the executable takes the place of glibc's for every call
// from the game. Native deviation, see docs/native-status.md.
#include <stdint.h>

namespace
{
uint64_t gSeed = 0; // musl: srand(1) and the initial state give the same sequence
}

extern "C" void srand(unsigned s)
{
    gSeed = s - 1;
}

extern "C" int rand(void)
{
    gSeed = 6364136223846793005ULL * gSeed + 1;
    return static_cast<int>(gSeed >> 33);
}
