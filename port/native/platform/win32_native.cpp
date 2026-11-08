// Win32 calls the engine makes that port/platform/win32_shim.cpp does not provide
// (the browser build never reaches them), plus the ImGui debug hook.
#include <string.h>

#include <windows.h>

extern "C" {

// Wide (UTF-16, -fshort-wchar) to the ANSI code page. The game only converts player
// names; characters outside ASCII become '?'. cchWideChar -1: up to and including the
// terminator. cbMultiByte 0: return the size needed.
int WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int cchWideChar, LPSTR dst, int cbMultiByte, LPCSTR, LPBOOL usedDefault)
{
    if (!src)
    {
        return 0;
    }
    int count = cchWideChar;
    if (count < 0)
    {
        count = 0;
        while (src[count])
        {
            ++count;
        }
        ++count; // the terminator
    }
    if (usedDefault)
    {
        *usedDefault = FALSE;
    }
    if (cbMultiByte == 0)
    {
        return count;
    }
    if (!dst || cbMultiByte < count)
    {
        return 0; // ERROR_INSUFFICIENT_BUFFER
    }
    for (int i = 0; i < count; ++i)
    {
        const unsigned c = static_cast<unsigned short>(src[i]);
        if (c < 0x80)
        {
            dst[i] = static_cast<char>(c);
        }
        else
        {
            dst[i] = '?';
            if (usedDefault)
            {
                *usedDefault = TRUE;
            }
        }
    }
    return count;
}

LPSTR lstrcatA(LPSTR dst, LPCSTR src)
{
    return strcat(dst, src);
}

} // extern "C"

// winmain.cpp hands this to gbh_SetBeginSceneCB only when the renderer exports that
// function, which the port's renderer does not (the ImGui debug UI is not built).
void ImGuiDebugDraw()
{
}
