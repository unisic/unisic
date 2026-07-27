#pragma once

// The signal-handler half of the diagnostic subsystem, in its own translation
// unit precisely so that nobody adds a QString to it by accident.
//
// RULE FOR THIS FILE AND ITS .cpp: async-signal-safe calls ONLY. The handler
// runs after the process is already broken, possibly with the heap lock held by
// the thread that faulted, so anything that allocates can deadlock instead of
// reporting. Legal here: write, open, close, raise, sigaction, sigprocmask,
// sigaltstack, alarm, time, getpid, _exit, backtrace. NOT legal: QString,
// snprintf, malloc, and specifically backtrace_symbols (it allocates -
// backtrace_symbols_fd does not).
//
// What it can capture: the crashing thread's frames as module+offset, which
// addr2line resolves against a build with the same layout. What it cannot:
// other threads, QML/V4 frames, variable values, or a SIGKILL/OOM kill - which
// is exactly why DiagLog's file exists independently of this.
namespace CrashHandler {

// Installs a 64 KiB sigaltstack and handlers for SIGSEGV/SIGABRT/SIGBUS/
// SIGFPE/SIGILL with SA_SIGINFO|SA_ONSTACK|SA_RESETHAND. SA_ONSTACK is what
// makes a stack-overflow SIGSEGV able to print anything at all; SA_RESETHAND
// means a fault inside the handler dies immediately instead of looping.
// Also warms backtrace() once so the unwinder's lazy dlopen of libgcc_s has
// already happened by the time the handler needs it.
// Compiled to a no-op under ASan/TSan so the sanitizer keeps reporting.
void install();

// The already-open log fd (or -1). The handler writes its block there too, so
// a crash report and the run that led to it live in one file.
void setLogFd(int fd);

// Where to write the standalone crash file. Copied into a static buffer; the
// handler never touches the caller's memory.
void setCrashFilePath(const char *utf8Path);

// One identifying line, e.g. "unisic 0.8 build 41 app=unisic pid=1234".
void setHeader(const char *utf8Header);

// Dev harness only: render the exact same block for a synthetic signal into
// `fd`. Raises nothing, kills nothing, so the smoke test can assert the shape
// of a real report without crashing the app to get one.
void devWriteSyntheticReport(int fd, int sig);

} // namespace CrashHandler
