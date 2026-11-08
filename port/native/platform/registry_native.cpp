// The game's Windows registry, kept in conf/registry.txt (native build).
//
// The registry itself lives in memory (port/platform/win32_shim.cpp), as in the browser,
// where the shell keeps gta2_port_reg_export()'s text in browser storage. Natively the
// same text goes to a file: one value per line, "path|name|d|number" or "path|name|s|text",
// '#' starts a comment. It is read at start-up, after the defaults (main.cpp), and written
// back when it has changed (checked every few seconds, and at exit), through a temporary
// file and a rename so a crash never leaves half a file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

// port_prefix.h (forced into every file) routes these to the game's versions, which
// lowercase and resolve game paths; this file opens its own absolute paths.
#undef fopen
#undef fclose
#undef remove

typedef unsigned int DWORD_; // windows.h's DWORD is 4 bytes natively (port/core/compat)

extern "C" void gta2_port_reg_set_dword(const char* path, const char* name, DWORD_ value);
extern "C" void gta2_port_reg_set_string(const char* path, const char* name, const char* value);
extern "C" const char* gta2_port_reg_export();

namespace
{
std::string gPath;
std::string gWritten; // the export last written (or read)

const char* const kHeader = "# GTA2 registry (the game's settings), written by the game.\n"
                            "# One value per line: key path|value name|d (number) or s (text)|value\n";
} // namespace

extern "C" {

int Gta2Native_RegistryLoad(const char* path)
{
    gPath = path;
    FILE* f = fopen(path, "r");
    if (!f)
    {
        return 0;
    }
    int count = 0;
    char line[2048];
    while (fgets(line, sizeof(line), f))
    {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            line[--n] = 0;
        }
        if (!n || line[0] == '#')
        {
            continue;
        }
        char* name = strchr(line, '|');
        char* type = name ? strchr(name + 1, '|') : nullptr;
        char* value = type ? strchr(type + 1, '|') : nullptr;
        if (!value)
        {
            fprintf(stderr, "gta2: %s: ignoring \"%s\"\n", path, line);
            continue;
        }
        *name++ = 0;
        *type++ = 0;
        *value++ = 0;
        if (!strcmp(type, "d"))
        {
            gta2_port_reg_set_dword(line, name, static_cast<DWORD_>(strtoul(value, nullptr, 10)));
            ++count;
        }
        else if (!strcmp(type, "s"))
        {
            gta2_port_reg_set_string(line, name, value);
            ++count;
        }
        else
        {
            fprintf(stderr, "gta2: %s: unknown type \"%s\"\n", path, type);
        }
    }
    fclose(f);
    fprintf(stderr, "gta2: %d registry values from %s\n", count, path);
    return count;
}

// A DWORD value of the registry, or fallback when it is not set. path and name are
// lowercase, as the registry keeps them (win32_shim.cpp). Reads the export, so it is meant
// for occasional use (input_sdl.cpp reads the 12 control keys when entering the city).
unsigned int Gta2Native_RegistryDword(const char* path, const char* name, unsigned int fallback)
{
    const std::string want = std::string(path) + "|" + name + "|d|";
    const std::string all = gta2_port_reg_export();
    for (size_t pos = 0; pos < all.size();)
    {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos)
        {
            end = all.size();
        }
        if (all.compare(pos, want.size(), want) == 0)
        {
            return static_cast<unsigned int>(strtoul(all.c_str() + pos + want.size(), nullptr, 10));
        }
        pos = end + 1;
    }
    return fallback;
}

// Writes the registry if it changed since the last write. Returns false on a write error.
bool Gta2Native_RegistrySave()
{
    if (gPath.empty())
    {
        return true;
    }
    const std::string now = gta2_port_reg_export();
    if (now == gWritten)
    {
        return true;
    }
    const std::string tmp = gPath + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f)
    {
        fprintf(stderr, "gta2: cannot write %s\n", tmp.c_str());
        return false;
    }
    const bool ok = fputs(kHeader, f) >= 0 && fwrite(now.data(), 1, now.size(), f) == now.size();
    if (fclose(f) != 0 || !ok || rename(tmp.c_str(), gPath.c_str()) != 0)
    {
        fprintf(stderr, "gta2: cannot write %s\n", gPath.c_str());
        remove(tmp.c_str());
        return false;
    }
    gWritten = now;
    return true;
}

} // extern "C"
