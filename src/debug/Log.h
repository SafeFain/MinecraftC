#pragma once

#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

// ── Compile-time log-level gating ──────────────────────────────────────
// LOG_COMPILE_LEVEL sets the minimum level compiled into the binary.
// Messages below this level are stripped by the preprocessor (zero cost).
//   0 = Trace   1 = Debug   2 = Info   3 = Warn   4 = Error   5 = Fatal
// Override via CMake: -DLOG_COMPILE_LEVEL=0
#ifndef LOG_COMPILE_LEVEL
#  ifdef NDEBUG
#    define LOG_COMPILE_LEVEL 2   // Release: strip Trace + Debug
#  else
#    define LOG_COMPILE_LEVEL 0   // Debug:   all messages compiled in
#  endif
#endif

namespace Debug {

enum class LogLevel : int {
    Trace = 0,   // Fine-grained tracing, variable values, per-frame detail
    Debug = 1,   // Flow control, state transitions, algorithm steps
    Info  = 2,   // User-visible milestones (startup, generation stats, etc.)
    Warn  = 3,   // Recoverable issues, fallback paths, degraded operation
    Error = 4,   // Non-fatal failures (shader compile errors, GL errors)
    Fatal = 5,   // Unrecoverable — the process will abort/exit afterward
    Off   = 6
};

// ── Thread-safe logging sink ──────────────────────────────────────────

class Log {
public:
    // Must be called once at startup before any LOG_* macro is used.
    // If fileOutput is true, messages are also appended to logPath.
    static void init(LogLevel minLevel = LogLevel::Info,
                     bool fileOutput = false,
                     const std::string& logPath = "minecraftc.log");

    // Write a message. Thread-safe — multiple threads can call concurrently
    // and each log line will be written atomically.
    static void log(LogLevel level, const char* file, int line,
                    const std::string& message);

    // Flush and close log file. Call before process exit.
    static void shutdown();

    // Change minimum level at runtime (takes effect immediately).
    static void setMinLevel(LogLevel level) { s_minLevel = level; }
    static LogLevel minLevel()            { return s_minLevel; }

private:
    static LogLevel     s_minLevel;
    static std::mutex   s_mutex;
    static std::ofstream s_file;
    static bool         s_fileOutput;
    static bool         s_colorOutput;   // auto-detected: true if stdout is a TTY

    static const char*  levelToString(LogLevel level);
    static const char*  levelToAnsi(LogLevel level);
    static std::string  timestamp();
};

} // namespace Debug

// ── Log macros ─────────────────────────────────────────────────────────
// Each macro captures __FILE__ and __LINE__ automatically.
// The stream-style API (LOG_INFO("x=" << x)) constructs the message via
// std::ostringstream before calling Log::log(), so string building happens
// outside the lock.
//
// Compile-time gating: when LOG_COMPILE_LEVEL > level, the macro body is
// stripped to ((void)0) by the preprocessor — zero runtime cost.

#define MC_LOG_IMPL(level, msg)                                                 \
    do {                                                                        \
        std::ostringstream _mc_log_oss;                                         \
        _mc_log_oss << msg;                                                     \
        Debug::Log::log((level), __FILE__, __LINE__, _mc_log_oss.str());        \
    } while (0)

#if LOG_COMPILE_LEVEL <= 0
#  define LOG_TRACE(msg) MC_LOG_IMPL(Debug::LogLevel::Trace, msg)
#else
#  define LOG_TRACE(msg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 1
#  define LOG_DEBUG(msg) MC_LOG_IMPL(Debug::LogLevel::Debug, msg)
#else
#  define LOG_DEBUG(msg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 2
#  define LOG_INFO(msg)  MC_LOG_IMPL(Debug::LogLevel::Info,  msg)
#else
#  define LOG_INFO(msg)  ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 3
#  define LOG_WARN(msg)  MC_LOG_IMPL(Debug::LogLevel::Warn,  msg)
#else
#  define LOG_WARN(msg)  ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 4
#  define LOG_ERROR(msg) MC_LOG_IMPL(Debug::LogLevel::Error, msg)
#else
#  define LOG_ERROR(msg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= 5
#  define LOG_FATAL(msg) MC_LOG_IMPL(Debug::LogLevel::Fatal, msg)
#else
#  define LOG_FATAL(msg) ((void)0)
#endif
