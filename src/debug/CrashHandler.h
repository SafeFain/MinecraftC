#pragma once

namespace Debug {

// Install signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL.
// On crash: captures a backtrace, writes it to stderr + log file,
// then re-raises the signal with the default handler (for core dump).
//
// Must be called once early in main(), before any threads are spawned.
// Returns true on success, false if signal handlers could not be installed.
bool installCrashHandlers();

// Print a stack trace at the current call site (up to `maxFrames` deep).
// Useful for non-fatal diagnostic logging.
void printStackTrace(int maxFrames = 32);

} // namespace Debug
