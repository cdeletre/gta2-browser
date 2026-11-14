/* Dumps a running program's memory image under Wine, for reading the original
 * game's unpacked code and initialised data (the Rockstar Classics gta2.exe is
 * TAC-packed on disk). Local analysis only; the dump stays in research/.
 *
 * Usage: gta2_memdump.exe <window-title> <start-hex> <end-hex> <out-file>
 * Unreadable pages are written as zero bytes, so file offset = address - start.
 */
typedef unsigned long DWORD;
typedef void *HANDLE;
typedef void *HWND;
typedef __SIZE_TYPE__ size_t;
#define WINAPI __stdcall
#define NULL ((void *)0)
#define PROCESS_VM_READ 0x0010
#define PROCESS_QUERY_INFORMATION 0x0400
typedef struct {
    void *BaseAddress, *AllocationBase;
    DWORD AllocationProtect;
    size_t RegionSize;
    DWORD State, Protect, Type;
} MEMORY_BASIC_INFORMATION;
#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 0x01
#define PAGE_GUARD 0x100
__declspec(dllimport) HWND WINAPI FindWindowA(const char *, const char *);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(HWND, DWORD *);
__declspec(dllimport) HANDLE WINAPI OpenProcess(DWORD, int, DWORD);
__declspec(dllimport) int WINAPI ReadProcessMemory(HANDLE, const void *, void *, size_t, size_t *);
__declspec(dllimport) size_t WINAPI VirtualQueryEx(HANDLE, const void *, MEMORY_BASIC_INFORMATION *, size_t);
__declspec(dllimport) int WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int printf(const char *, ...);
__declspec(dllimport) void *fopen(const char *, const char *);
__declspec(dllimport) size_t fwrite(const void *, size_t, size_t, void *);
__declspec(dllimport) int fclose(void *);
__declspec(dllimport) unsigned long strtoul(const char *, char **, int);
__declspec(dllimport) void exit(int);
__declspec(dllimport) int __getmainargs(int *, char ***, char ***, int, void *);

static unsigned char page[4096];

static int main(int argc, char **argv)
{
    if (argc != 5) {
        printf("usage: gta2_memdump <window-title> <start-hex> <end-hex> <out-file>\n");
        return 2;
    }
    HWND w = FindWindowA(NULL, argv[1]);
    if (!w) { printf("window not found\n"); return 1; }
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, 0, pid);
    if (!p) { printf("cannot open process %lu\n", pid); return 1; }
    unsigned long start = strtoul(argv[2], NULL, 16), end = strtoul(argv[3], NULL, 16);
    void *f = fopen(argv[4], "wb");
    if (!f) { printf("cannot write %s\n", argv[4]); return 1; }
    unsigned long readable = 0;
    for (unsigned long a = start; a < end; a += sizeof page) {
        MEMORY_BASIC_INFORMATION mbi;
        size_t got = 0;
        int ok = VirtualQueryEx(p, (void *)a, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && ReadProcessMemory(p, (void *)a, page, sizeof page, &got) &&
            got == sizeof page;
        if (ok) readable += sizeof page;
        else for (unsigned i = 0; i < sizeof page; i++) page[i] = 0;
        fwrite(page, 1, sizeof page, f);
    }
    fclose(f);
    CloseHandle(p);
    printf("pid %lu: %lu of %lu bytes readable\n", pid, readable, end - start);
    return 0;
}

void mainCRTStartup(void)
{
    int argc, si = 0;
    char **argv, **envp;
    __getmainargs(&argc, &argv, &envp, 0, &si);
    exit(main(argc, argv));
}
