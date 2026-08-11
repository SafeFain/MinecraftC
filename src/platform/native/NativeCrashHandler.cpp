#include "debug/CrashHandler.h"
#include "debug/Log.h"

#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

// POSIX backtrace support is available on glibc and macOS.
#if defined(__GLIBC__) || (defined(__APPLE__) && !TARGET_OS_IPHONE)
#  include <execinfo.h>
#  define HAS_BACKTRACE 1
#else
#  define HAS_BACKTRACE 0
#endif

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace Debug {

// ── Internal signal handler ────────────────────────────────────────────

// Async-signal-safe: only calls functions safe to use in signal handlers.
#if !defined(_WIN32)
static void crashSignalHandler(int sig) {
    // Build a minimal crash message (no heap allocation, no iostream)
    const char* sigName = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: sigName = "SIGSEGV"; break;
        case SIGABRT: sigName = "SIGABRT"; break;
        case SIGFPE:  sigName = "SIGFPE";  break;
        case SIGILL:  sigName = "SIGILL";  break;
        default: break;
    }

    // write() is async-signal-safe
    const char banner[] = "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                          "[CRASH] MinecraftC caught signal: ";
    write(STDERR_FILENO, banner, sizeof(banner) - 1);
    write(STDERR_FILENO, sigName, std::strlen(sigName));
    write(STDERR_FILENO, "\n", 1);

#if HAS_BACKTRACE
    write(STDERR_FILENO, "[CRASH] Stack trace:\n", 20);

    void* frames[64];
    int frameCount = backtrace(frames, 64);
    // backtrace_symbols_fd is async-signal-safe (writes directly to fd)
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
#endif

    write(STDERR_FILENO, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n",
          sizeof("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n") - 1);

    // Re-raise with default handler to get a core dump
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#else
static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exception) {
    char message[128]{};
    const unsigned long code = exception && exception->ExceptionRecord
        ? exception->ExceptionRecord->ExceptionCode : 0;
    const int length = std::snprintf(
        message, sizeof(message),
        "\n[CRASH] MinecraftC caught Windows exception 0x%08lX\n", code);
    const HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (stderrHandle != INVALID_HANDLE_VALUE && length > 0) {
        DWORD written = 0;
        WriteFile(stderrHandle, message, static_cast<DWORD>(length), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// ── Public API ─────────────────────────────────────────────────────────

bool installCrashHandlers() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    LOG_DEBUG("Windows unhandled-exception diagnostics installed");
#else
#if HAS_BACKTRACE
    LOG_DEBUG("Crash handlers installed (backtrace available)");
#else
    LOG_DEBUG("Crash handlers installed (no backtrace support)");
#endif

    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGFPE,  crashSignalHandler);
    std::signal(SIGILL,  crashSignalHandler);
#endif

    return true;
}

void printStackTrace(int maxFrames) {
#if defined(_WIN32)
    if (maxFrames < 0) maxFrames = 0;
    if (maxFrames > 64) maxFrames = 64;
    void* frames[64]{};
    const USHORT frameCount = CaptureStackBackTrace(
        0, static_cast<DWORD>(maxFrames), frames, nullptr);
    for (USHORT i = 0; i < frameCount; ++i)
        LOG_TRACE("[STACK] #" << i << " " << frames[i]);
#elif HAS_BACKTRACE
    void* frames[64];
    if (maxFrames > 64) maxFrames = 64;

    int frameCount = backtrace(frames, maxFrames);
    char** symbols = backtrace_symbols(frames, frameCount);

    if (symbols) {
        for (int i = 0; i < frameCount; ++i) {
            // Write via Log so it goes to both console and file
            // (note: printStackTrace is typically called from non-signal context)
            LOG_TRACE("[STACK] #" << i << " " << symbols[i]);
        }
        std::free(symbols);
    }
#else
    (void)maxFrames;
    LOG_TRACE("[STACK] backtrace not available on this platform");
#endif
}

} // namespace Debug
