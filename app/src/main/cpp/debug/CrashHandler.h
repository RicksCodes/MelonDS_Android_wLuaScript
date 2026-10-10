#ifndef MELONDS_CRASH_HANDLER_H
#define MELONDS_CRASH_HANDLER_H

namespace MelonDSAndroid::Debug
{

/**
 * Installs signal handlers that write a short native crash report (signal, faulting address, backtrace as library
 * offsets, last breadcrumb) to the given file, then hand the signal over to the system so that the usual tombstone is
 * still produced. Diagnostic aid for the Lua scripting build.
 */
void installCrashHandler(const char* reportPath);

/**
 * Records what the emulator is doing. Must be a string literal (only the pointer is stored).
 */
void setBreadcrumb(const char* breadcrumb);

}

#define CRASH_BREADCRUMB(text) MelonDSAndroid::Debug::setBreadcrumb(text)

#endif
