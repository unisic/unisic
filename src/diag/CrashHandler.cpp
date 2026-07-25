#include "CrashHandler.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <initializer_list>

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

// Async-signal-safe only. See the header for the full rule; the short version
// is that every helper below writes bytes with write(2) and formats numbers by
// hand, because snprintf and QString are both allowed to allocate.

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define UNISIC_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define UNISIC_SANITIZED 1
#endif
#endif

namespace {

constexpr int kMaxFrames = 64;

char g_crashPath[4096] = {0};
char g_header[256] = {0};
int g_logFd = -1;
void *g_frames[kMaxFrames];
// SIGSTKSZ stopped being a compile-time constant in glibc 2.34, so the alt
// stack is a fixed buffer rather than a VLA or an allocation.
char g_altStack[64 * 1024];
std::atomic_flag g_inHandler = ATOMIC_FLAG_INIT;

void writeAll(int fd, const char *p, size_t n)
{
    if (fd < 0)
        return;
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w <= 0) {
            if (w < 0 && errno == EINTR)
                continue;
            return;
        }
        p += w;
        n -= size_t(w);
    }
}

void writeStr(int fd, const char *s)
{
    if (s)
        writeAll(fd, s, ::strlen(s));
}

void writeDec(int fd, long v)
{
    char buf[24];
    int i = int(sizeof(buf));
    const bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    if (u == 0)
        buf[--i] = '0';
    while (u > 0) {
        buf[--i] = char('0' + (u % 10));
        u /= 10;
    }
    if (neg)
        buf[--i] = '-';
    writeAll(fd, buf + i, size_t(int(sizeof(buf)) - i));
}

void writeHex(int fd, unsigned long v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 2 * sizeof(unsigned long)];
    int i = int(sizeof(buf));
    if (v == 0)
        buf[--i] = '0';
    while (v > 0) {
        buf[--i] = digits[v & 0xf];
        v >>= 4;
    }
    buf[--i] = 'x';
    buf[--i] = '0';
    writeAll(fd, buf + i, size_t(int(sizeof(buf)) - i));
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort, usually a failed assertion or a C++ exception nobody caught)";
    case SIGBUS:  return "SIGBUS (bad memory alignment or a truncated mapped file)";
    case SIGFPE:  return "SIGFPE (arithmetic fault)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    default:      return "unknown signal";
    }
}

// The whole report, written to one fd. Shared by the real handler and the dev
// harness so the two can never drift into printing different things.
void writeReport(int fd, int sig, void *faultAddr, bool haveFrames, int frameCount)
{
    writeStr(fd, "\n=== unisic crash report ===\n");
    writeStr(fd, g_header);
    writeStr(fd, "\nwhen: unix ");
    writeDec(fd, long(::time(nullptr)));
    writeStr(fd, "\nsignal: ");
    writeDec(fd, sig);
    writeStr(fd, " ");
    writeStr(fd, signalName(sig));
    if (faultAddr) {
        writeStr(fd, "\nfault address: ");
        writeHex(fd, (unsigned long)faultAddr);
    }
    writeStr(fd, "\nthread: crashing thread only; other threads, QML frames and\n"
                 "        variable values are not recoverable from a signal handler.\n");
    writeStr(fd, "backtrace (module+offset; run addr2line against a matching build):\n");
    if (haveFrames && frameCount > 0) {
        // backtrace_symbols_fd, never backtrace_symbols: the latter allocates.
        ::backtrace_symbols_fd(g_frames, frameCount, fd);
    } else {
        writeStr(fd, "  (no frames: the unwinder could not walk this stack)\n");
    }
    writeStr(fd, "=== end crash report ===\n");
}

extern "C" void crashSignalHandler(int sig, siginfo_t *info, void *)
{
    // A second fault while reporting must not loop. SA_RESETHAND already
    // restores SIG_DFL for the same signal; this covers a different one
    // arriving on another thread.
    if (g_inHandler.test_and_set())
        ::_exit(128 + sig);

    // Even a wedged handler has to end: if the unwinder blocks on a lock the
    // faulting thread held, this is what still produces a core.
    ::alarm(5);

    const int n = ::backtrace(g_frames, kMaxFrames);

    writeReport(2, sig, info ? info->si_addr : nullptr, true, n);
    if (g_logFd >= 0)
        writeReport(g_logFd, sig, info ? info->si_addr : nullptr, true, n);
    if (g_crashPath[0]) {
        const int fd = ::open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            writeReport(fd, sig, info ? info->si_addr : nullptr, true, n);
            ::close(fd);
        }
    }

    // Hand the process back to the kernel with the right signal so
    // systemd-coredump still records a core: restore the default disposition,
    // unblock the signal (it is blocked while its own handler runs) and
    // re-raise.
    struct sigaction dfl;
    ::memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    ::sigemptyset(&dfl.sa_mask);
    ::sigaction(sig, &dfl, nullptr);
    sigset_t unblock;
    ::sigemptyset(&unblock);
    ::sigaddset(&unblock, sig);
    ::sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
    ::raise(sig);
    ::_exit(128 + sig);
}

} // namespace

namespace CrashHandler {

void install()
{
#ifdef UNISIC_SANITIZED
    // ASan/TSan install their own handlers and produce a far better report;
    // taking the signal from them would trade that for this.
    return;
#else
    // Warm up the unwinder while the process is still healthy: the first
    // backtrace() dlopen()s libgcc_s, which is not something to attempt for
    // the first time inside a signal handler.
    void *warm[1];
    (void)::backtrace(warm, 1);

    stack_t ss;
    ::memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);

    struct sigaction sa;
    ::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    ::sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        ::sigaction(sig, &sa, nullptr);
#endif
}

void setLogFd(int fd) { g_logFd = fd; }

void setCrashFilePath(const char *utf8Path)
{
    if (!utf8Path) {
        g_crashPath[0] = '\0';
        return;
    }
    ::strncpy(g_crashPath, utf8Path, sizeof(g_crashPath) - 1);
    g_crashPath[sizeof(g_crashPath) - 1] = '\0';
}

void setHeader(const char *utf8Header)
{
    if (!utf8Header) {
        g_header[0] = '\0';
        return;
    }
    ::strncpy(g_header, utf8Header, sizeof(g_header) - 1);
    g_header[sizeof(g_header) - 1] = '\0';
}

void devWriteSyntheticReport(int fd, int sig)
{
    const int n = ::backtrace(g_frames, kMaxFrames);
    writeReport(fd, sig, nullptr, true, n);
}

} // namespace CrashHandler
