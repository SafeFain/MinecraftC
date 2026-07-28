#pragma once

namespace Debug {

// Install POSIX signal handlers or the Windows unhandled-exception filter.
// Diagnostics are written directly to stderr; supported POSIX platforms also
// include a native backtrace before re-raising for the default crash behavior.
//
// Must be called once early in main(), before any threads are spawned.
// Returns true on success, false if signal handlers could not be installed.
bool installCrashHandlers();

// Print a stack trace at the current call site (up to `maxFrames` deep).
// Useful for non-fatal diagnostic logging.
void printStackTrace(int maxFrames = 32);

} // namespace Debug
