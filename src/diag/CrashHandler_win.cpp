// Windows build of CrashHandler: same API and the same report block as the
// Linux CrashHandler.cpp, so DiagLog, the crash file and the smoke test see one
// format. A structured-exception filter replaces the signal handlers (SIGABRT
// is still a CRT signal and keeps a handler), and RtlCaptureStackBackTrace +
// the loader's module lookup replace backtrace_symbols_fd.
//
// The rule from CrashHandler.h holds here too: nothing in the report path
// allocates. Paths are converted to UTF-16 when they are set, never inside the
// handler.
#include "CrashHandler.h"

#include <atomic>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <io.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#include <windows.h>

namespace {

constexpr int kMaxFrames = 64;

wchar_t g_crashPath[MAX_PATH * 4] = {0};
char g_header[256] = {0};
int g_logFd = -1;
void *g_frames[kMaxFrames];
std::atomic_flag g_inHandler = ATOMIC_FLAG_INIT;

void writeAll(int fd, const char *p, size_t n)
{
    if (fd < 0)
        return;
    while (n > 0) {
        const int w = ::_write(fd, p, unsigned(n));
        if (w <= 0)
            return;
        p += w;
        n -= size_t(w);
    }
}

void writeStr(int fd, const char *s)
{
    if (s)
        writeAll(fd, s, ::strlen(s));
}

void writeDec(int fd, long long v)
{
    char buf[24];
    int i = int(sizeof(buf));
    const bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
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

void writeHex(int fd, unsigned long long v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 2 * sizeof(unsigned long long)];
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

// Same wording as the Linux build for the signals both have, so a report reads
// the same whichever OS wrote it.
const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort, usually a failed assertion or a C++ exception nobody caught)";
    case SIGFPE:  return "SIGFPE (arithmetic fault)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    default:      return "unknown signal";
    }
}

// The POSIX signal a structured exception corresponds to, for the "signal:"
// line; the raw code follows it on its own line.
int signalFor(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return SIGSEGV;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        return SIGILL;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return SIGFPE;
    default:
        return 0;
    }
}

// "  unisic.exe+0x1a2b3c": module base name plus the offset from its load
// address. GetModuleHandleExW with UNCHANGED_REFCOUNT and GetModuleFileNameA
// only read the loader's tables; neither allocates.
void writeFrame(int fd, void *addr)
{
    writeStr(fd, "  ");
    HMODULE mod = nullptr;
    char path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(addr), &mod)
        && mod && GetModuleFileNameA(mod, path, sizeof(path)) > 0) {
        path[sizeof(path) - 1] = '\0';
        const char *base = path;
        for (const char *p = path; *p; ++p)
            if (*p == '\\' || *p == '/')
                base = p + 1;
        writeStr(fd, base);
        writeStr(fd, "+");
        writeHex(fd, (unsigned long long)((const char *)addr - (const char *)mod));
    } else {
        writeHex(fd, (unsigned long long)addr);
    }
    writeStr(fd, "\n");
}

// The whole report, written to one fd. Shared by the real handlers and the dev
// harness so the two can never drift into printing different things.
void writeReport(int fd, int sig, unsigned long code, void *faultAddr, int frameCount)
{
    writeStr(fd, "\n=== unisic crash report ===\n");
    writeStr(fd, g_header);
    writeStr(fd, "\nwhen: unix ");
    writeDec(fd, (long long)::time(nullptr));
    writeStr(fd, "\nsignal: ");
    writeDec(fd, sig);
    writeStr(fd, " ");
    writeStr(fd, signalName(sig));
    if (code) {
        writeStr(fd, "\nexception: ");
        writeHex(fd, code);
    }
    if (faultAddr) {
        writeStr(fd, "\nfault address: ");
        writeHex(fd, (unsigned long long)faultAddr);
    }
    writeStr(fd, "\nthread: crashing thread only; other threads, QML frames and\n"
                 "        variable values are not recoverable from a crash handler.\n");
    writeStr(fd, "backtrace (module+offset; run addr2line against a matching build):\n");
    if (frameCount > 0) {
        for (int i = 0; i < frameCount; ++i)
            writeFrame(fd, g_frames[i]);
    } else {
        writeStr(fd, "  (no frames: the unwinder could not walk this stack)\n");
    }
    writeStr(fd, "=== end crash report ===\n");
}

void reportEverywhere(int sig, unsigned long code, void *faultAddr)
{
    const int n = int(RtlCaptureStackBackTrace(0, kMaxFrames, g_frames, nullptr));
    writeReport(2, sig, code, faultAddr, n);
    if (g_logFd >= 0) {
        writeReport(g_logFd, sig, code, faultAddr, n);
        ::_commit(g_logFd);
    }
    if (g_crashPath[0]) {
        const int fd = ::_wopen(g_crashPath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                                _S_IREAD | _S_IWRITE);
        if (fd >= 0) {
            writeReport(fd, sig, code, faultAddr, n);
            ::_close(fd);
        }
    }
}

LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *rec = info ? info->ExceptionRecord : nullptr;
    const DWORD code = rec ? rec->ExceptionCode : 0;
    // A second fault while reporting must not loop.
    if (g_inHandler.test_and_set())
        return EXCEPTION_CONTINUE_SEARCH;
    void *fault = nullptr;
    if (rec && code == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        fault = reinterpret_cast<void *>(rec->ExceptionInformation[1]);
    else if (rec)
        fault = rec->ExceptionAddress;
    reportEverywhere(signalFor(code), code, fault);
    // Unlike systemd-coredump, Windows Error Reporting keeps no local dump
    // unless the user configured LocalDumps, and handing it a crash from inside
    // DLL detach held the process for most of a minute. The report is written;
    // end now, with the exception code as the exit status.
    TerminateProcess(GetCurrentProcess(), code);
    return EXCEPTION_EXECUTE_HANDLER;
}

extern "C" void crashAbortHandler(int sig)
{
    if (!g_inHandler.test_and_set())
        reportEverywhere(sig, 0, nullptr);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
    ::_exit(128 + sig);
}

} // namespace

namespace CrashHandler {

void install()
{
    SetUnhandledExceptionFilter(crashExceptionFilter);
    ::signal(SIGABRT, crashAbortHandler);
}

void setLogFd(int fd) { g_logFd = fd; }

void setCrashFilePath(const char *utf8Path)
{
    g_crashPath[0] = L'\0';
    if (!utf8Path)
        return;
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, g_crashPath,
                                      int(sizeof(g_crashPath) / sizeof(g_crashPath[0])));
    if (n <= 0)
        g_crashPath[0] = L'\0';
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
    const int n = int(RtlCaptureStackBackTrace(0, kMaxFrames, g_frames, nullptr));
    writeReport(fd, sig, 0, nullptr, n);
}

} // namespace CrashHandler
