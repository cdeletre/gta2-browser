// The remaining replaceable operator new/delete forms, on the engine's allocator.
//
// Source/crt_stubs.cpp replaces the plain operator new/delete (and new[]/delete[]) with the
// game's zero-filling crt::malloc and crt::free. C++ expects every form to be replaced
// together: otherwise a library that allocates with new(nothrow) gets the runtime's
// allocator and frees with the engine's delete. Observed in the build container under
// ASan: LLVM (Mesa's llvmpipe) allocated with ASan's new(nothrow) and released through
// crt_stubs.cpp's delete (alloc-dealloc-mismatch). The aligned (align_val_t) forms stay the
// runtime's, as a pair: crt::malloc cannot honour extended alignment.
#include <stddef.h>

#include <new>

namespace crt
{
void* malloc(size_t size); // Source/crt_stubs.cpp (zero-filled)
void free(void* p);
} // namespace crt

void* operator new(size_t n, const std::nothrow_t&) noexcept
{
    return crt::malloc(n);
}

void* operator new[](size_t n, const std::nothrow_t&) noexcept
{
    return crt::malloc(n);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    crt::free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    crt::free(p);
}

void operator delete(void* p, size_t) noexcept
{
    crt::free(p);
}

void operator delete[](void* p, size_t) noexcept
{
    crt::free(p);
}
