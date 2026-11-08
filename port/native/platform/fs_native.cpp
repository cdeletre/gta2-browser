// Case-insensitive path lookup for the native build.
//
// The game uses Windows paths with mixed case ("data\Keyboard\eng_kb.cfg"); the web shell
// stores the files lowercased, so port/platform/crt_compat.cpp lowercases every path.
// Linux filesystems (ext4 on the console) are case-sensitive and keep the installer's
// names, so natively each lowercased path is matched against the disk, one component at a
// time, ignoring case. Directory listings are cached; a name not found triggers one
// re-read of that directory (files the game created since).
//
// A component that matches nothing (a file about to be created, a wildcard) is kept as
// given, and the rest of the path after it too.
//
// The game's save folder, player\ (profiles, church saves, high scores), is redirected to
// the port's saves/ folder (Gta2Native_SetSaveDir, from main.cpp): the game runs in
// gamedata/, which holds only the installed game files.
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <string>
#include <unordered_map>

namespace
{
std::string gSaveDir; // absolute, no trailing slash; empty: no redirection
using Listing = std::unordered_map<std::string, std::string>; // lowercase -> name on disk
std::unordered_map<std::string, Listing> gDirs;

std::string Lower(const std::string& s)
{
    std::string out(s);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

const Listing& Read(const std::string& dir, bool refresh)
{
    auto it = gDirs.find(dir);
    if (it != gDirs.end() && !refresh)
    {
        return it->second;
    }
    Listing& listing = gDirs[dir];
    listing.clear();
    if (DIR* d = opendir(dir.empty() ? "." : dir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            listing.emplace(Lower(e->d_name), e->d_name);
        }
        closedir(d);
    }
    return listing;
}

bool Exists(const std::string& path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}
} // namespace

extern "C" void Gta2Native_SetSaveDir(const char* dir)
{
    gSaveDir = dir ? dir : "";
    while (gSaveDir.size() > 1 && gSaveDir.back() == '/')
    {
        gSaveDir.pop_back();
    }
}

std::string Gta2Native_ResolveCase(const std::string& lowered)
{
    // player\... -> saves/... (the path arrives lowercased, see crt_compat.cpp)
    const bool inPlayer = lowered == "player" || lowered.compare(0, 7, "player/") == 0;
    const std::string path = inPlayer && !gSaveDir.empty() ? gSaveDir + lowered.substr(6) : lowered;
    std::string out = path.size() && path[0] == '/' ? "/" : "";
    size_t pos = out.size();
    bool resolving = true;
    while (pos <= path.size())
    {
        size_t end = path.find('/', pos);
        if (end == std::string::npos)
        {
            end = path.size();
        }
        const std::string part = path.substr(pos, end - pos);
        pos = end + 1;
        if (part.empty())
        {
            continue;
        }
        std::string dir = out;
        if (dir.size() > 1 && dir.back() == '/')
        {
            dir.pop_back();
        }
        const std::string joined = out.empty() || out.back() == '/' ? out + part : out + "/" + part;
        if (!resolving || part == "." || part == ".." || Exists(joined))
        {
            out = joined;
            continue;
        }
        const std::string key = Lower(part);
        const Listing* listing = &Read(dir, false);
        auto it = listing->find(key);
        if (it == listing->end())
        {
            listing = &Read(dir, true);
            it = listing->find(key);
        }
        if (it == listing->end())
        {
            resolving = false; // not on disk: keep the rest as given
            out = joined;
            continue;
        }
        out = out.empty() || out.back() == '/' ? out + it->second : out + "/" + it->second;
    }
    return out;
}
