// Crash report for the native build: on SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT the log
// gets the signal, the faulting address, pc and lr, the game's status line and the call
// stack (module + offset, and the address), then the signal is raised again: SDL's own
// handler, installed before this one, still runs, and the launcher still sees the crash.
// gta2.aarch64 is not position-independent, so its frames read back from the address in
// brackets with the .debug file of the same release build (scripts/build_native.sh):
//   addr2line -f -C -i -e gta2.aarch64.debug 0x<address> ...
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

extern "C" void Gta2Native_StatusLine(char* out, int size); // status_native.cpp

namespace
{
char gAltStack[64 * 1024];
const int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
struct sigaction gPrevious[sizeof(kSignals) / sizeof(kSignals[0])];

void Say(const char* s)
{
    ssize_t r = write(STDERR_FILENO, s, strlen(s));
    (void)r;
}

void Frame(int i, void* addr)
{
    char line[512];
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_fname)
    {
        const char* name = strrchr(info.dli_fname, '/');
        name = name ? name + 1 : info.dli_fname;
        snprintf(line, sizeof(line), "gta2: crash:   #%d %s+0x%lx [%p]%s%s\n", i, name,
                 static_cast<unsigned long>(static_cast<char*>(addr) - static_cast<char*>(info.dli_fbase)), addr,
                 info.dli_sname ? " " : "", info.dli_sname ? info.dli_sname : "");
    }
    else
    {
        snprintf(line, sizeof(line), "gta2: crash:   #%d %p\n", i, addr);
    }
    Say(line);
}

void OnCrash(int sig, siginfo_t* si, void* ctx)
{
    static volatile sig_atomic_t inside = 0;
    if (!inside)
    {
        inside = 1;
        char line[512];
        snprintf(line, sizeof(line), "gta2: crash: signal %d (%s), address %p\n", sig, strsignal(sig), si ? si->si_addr : nullptr);
        Say(line);
#if defined(__aarch64__)
        const ucontext_t* uc = static_cast<const ucontext_t*>(ctx);
        Say("gta2: crash: pc and lr:\n");
        Frame(0, reinterpret_cast<void*>(uc->uc_mcontext.pc));
        Frame(1, reinterpret_cast<void*>(uc->uc_mcontext.regs[30]));
#else
        (void)ctx;
#endif
        Gta2Native_StatusLine(line, sizeof(line));
        Say("gta2: crash: game: ");
        Say(line);
        Say("\ngta2: crash: stack:\n");
        void* frames[48];
        const int n = backtrace(frames, 48);
        for (int i = 0; i < n; ++i)
        {
            Frame(i, frames[i]);
        }
    }
    // The handler that was there before (SDL's), else the default action.
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i)
    {
        if (kSignals[i] == sig)
        {
            sigaction(sig, &gPrevious[i], nullptr);
            raise(sig);
            return;
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
} // namespace

extern "C" void Gta2Native_InstallCrashHandler()
{
    void* warm[1];
    backtrace(warm, 1); // loads the unwinder now, not in the handler
    stack_t ss = {};
    ss.ss_sp = gAltStack;
    ss.ss_size = sizeof(gAltStack);
    sigaltstack(&ss, nullptr);
    struct sigaction sa = {};
    sa.sa_sigaction = OnCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i)
    {
        sigaction(kSignals[i], &sa, &gPrevious[i]);
    }
}
