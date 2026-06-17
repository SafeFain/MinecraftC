#include "debug/Log.h"

#include <iostream>
#include <unistd.h>

namespace Debug {

LogLevel     Log::s_minLevel   = LogLevel::Info;
std::mutex   Log::s_mutex;
std::ofstream Log::s_file;
bool         Log::s_fileOutput = false;
bool         Log::s_colorOutput = false;

// ── Initialization ─────────────────────────────────────────────────────

void Log::init(LogLevel minLevel, bool fileOutput, const std::string& logPath) {
    s_minLevel  = minLevel;
    s_fileOutput = fileOutput;

    // Auto-detect whether stdout is a terminal (for ANSI color codes)
    s_colorOutput = isatty(fileno(stdout));

    if (s_fileOutput) {
        s_file.open(logPath, std::ios::out | std::ios::app);
        if (!s_file.is_open()) {
            std::cerr << "[Log] Warning: failed to open log file: " << logPath << std::endl;
            s_fileOutput = false;
        }
    }
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file.is_open()) {
        s_file.flush();
        s_file.close();
    }
}

// ── Core write path ────────────────────────────────────────────────────

void Log::log(LogLevel level, const char* file, int line, const std::string& message) {
    // Runtime level filter (compiled-in messages may still be below minLevel)
    if (level < s_minLevel) return;

    std::string lineBuf;
    lineBuf.reserve(128 + message.size());
    lineBuf += '[';
    lineBuf += timestamp();
    lineBuf += "] [";
    lineBuf += levelToString(level);
    lineBuf += "] [";
    lineBuf += file;
    lineBuf += ':';
    lineBuf += std::to_string(line);
    lineBuf += "] ";
    lineBuf += message;
    lineBuf += '\n';

    std::lock_guard<std::mutex> lock(s_mutex);

    // Console output (possibly colorized)
    if (s_colorOutput) {
        std::cout << levelToAnsi(level) << lineBuf << "\033[0m";
    } else {
        std::cout << lineBuf;
    }
    std::cout.flush();

    // File output (plain, no ANSI)
    if (s_fileOutput && s_file.is_open()) {
        s_file << lineBuf;
        s_file.flush();
    }
}

// ── Helpers ────────────────────────────────────────────────────────────

const char* Log::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "?????";
    }
}

const char* Log::levelToAnsi(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\033[90m";   // bright black (gray)
        case LogLevel::Debug: return "\033[37m";   // white
        case LogLevel::Info:  return "\033[97m";   // bright white
        case LogLevel::Warn:  return "\033[33m";   // yellow
        case LogLevel::Error: return "\033[31m";   // red
        case LogLevel::Fatal: return "\033[91m";   // bright red
        default:              return "";
    }
}

std::string Log::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace Debug
