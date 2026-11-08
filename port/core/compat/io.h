/* _findfirst family over the in-memory game filesystem (port/platform). */
#pragma once
#include <stdint.h>
#include <time.h>
struct _finddata_t
{
    unsigned attrib;
    time_t time_create, time_access, time_write;
#if defined(__LP64__)
    unsigned int size; /* 4 bytes, as MSVC's `unsigned long` (8 on LP64 Linux) */
#else
    unsigned long size;
#endif
    char name[260];
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_SUBDIR 0x10
#ifdef __cplusplus
extern "C" {
#endif
intptr_t _findfirst(const char* pattern, struct _finddata_t* data);
int _findnext(intptr_t handle, struct _finddata_t* data);
int _findclose(intptr_t handle);
#ifdef __cplusplus
}
#endif
