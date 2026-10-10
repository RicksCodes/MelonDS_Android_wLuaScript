#include "CrashHandler.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

namespace MelonDSAndroid::Debug
{

namespace
{

constexpr int kSignals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
constexpr int kSignalCount = sizeof(kSignals) / sizeof(kSignals[0]);
constexpr int kMaxFrames = 48;

struct sigaction previousActions[kSignalCount];
char reportPath[512] = { 0 };
std::atomic<const char*> currentBreadcrumb { "(nessuna)" };
std::atomic_bool installed { false };
std::atomic_bool handling { false };

// --- Async-signal-safe output helpers (no malloc, no stdio) ---

void writeString(int fd, const char* text)
{
    if (text)
        write(fd, text, strlen(text));
}

void writeHex(int fd, uintptr_t value)
{
    char buffer[2 + sizeof(uintptr_t) * 2];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (size_t i = 0; i < sizeof(uintptr_t) * 2; ++i)
    {
        int shift = (int) ((sizeof(uintptr_t) * 2 - 1 - i) * 4);
        buffer[2 + i] = "0123456789abcdef"[(value >> shift) & 0xF];
    }
    write(fd, buffer, sizeof(buffer));
}

void writeDecimal(int fd, long value)
{
    char buffer[24];
    int position = sizeof(buffer);
    bool negative = value < 0;
    unsigned long magnitude = negative ? 0UL - (unsigned long) value : (unsigned long) value;
    do
    {
        buffer[--position] = (char) ('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (magnitude != 0 && position > 1);
    if (negative)
        buffer[--position] = '-';
    write(fd, buffer + position, sizeof(buffer) - position);
}

const char* fileName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void writeAddress(int fd, const char* label, uintptr_t address)
{
    writeString(fd, label);
    writeHex(fd, address);

    Dl_info info;
    if (address != 0 && dladdr(reinterpret_cast<void*>(address), &info) != 0 && info.dli_fname)
    {
        writeString(fd, "  ");
        writeString(fd, fileName(info.dli_fname));
        writeString(fd, " +");
        writeHex(fd, address - reinterpret_cast<uintptr_t>(info.dli_fbase));
        if (info.dli_sname)
        {
            writeString(fd, "  (");
            writeString(fd, info.dli_sname);
            writeString(fd, " +");
            writeDecimal(fd, (long) (address - reinterpret_cast<uintptr_t>(info.dli_saddr)));
            writeString(fd, ")");
        }
    }
    writeString(fd, "\n");
}

const char* signalName(int signal)
{
    switch (signal)
    {
        case SIGSEGV: return "SIGSEGV (accesso a memoria non valida)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL (istruzione non valida)";
        case SIGFPE: return "SIGFPE";
        case SIGTRAP: return "SIGTRAP";
        default: return "?";
    }
}

// Reads memory without crashing if the address is not mapped
bool safeRead(uintptr_t address, void* out, size_t size)
{
    iovec local { out, size };
    iovec remote { reinterpret_cast<void*>(address), size };
    return syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) == (long) size;
}

struct UnwindState
{
    int fd;
    int count;
};

_Unwind_Reason_Code unwindCallback(_Unwind_Context* context, void* argument)
{
    auto state = static_cast<UnwindState*>(argument);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc == 0)
        return _URC_END_OF_STACK;

    writeString(state->fd, "  #");
    writeDecimal(state->fd, state->count);
    writeAddress(state->fd, " ", pc);
    return ++state->count >= kMaxFrames ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void writeReport(int fd, int signal, siginfo_t* info, void* rawContext)
{
    writeString(fd, "Segnale: ");
    writeDecimal(fd, signal);
    writeString(fd, " ");
    writeString(fd, signalName(signal));
    writeString(fd, ", codice ");
    writeDecimal(fd, info ? info->si_code : 0);
    writeString(fd, "\nIndirizzo del fault: ");
    writeHex(fd, info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0);

    char threadName[32] = { 0 };
    prctl(PR_GET_NAME, threadName, 0, 0, 0);
    writeString(fd, "\nThread: ");
    writeString(fd, threadName);
    writeString(fd, " (tid ");
    writeDecimal(fd, (long) gettid());
    writeString(fd, ")\nUltima operazione: ");
    writeString(fd, currentBreadcrumb.load());
    writeString(fd, "\n\nRegistri:\n");

    auto context = static_cast<ucontext_t*>(rawContext);
    uintptr_t framePointer = 0;
#if defined(__aarch64__)
    writeAddress(fd, "  pc ", context->uc_mcontext.pc);
    writeAddress(fd, "  lr ", context->uc_mcontext.regs[30]);
    writeAddress(fd, "  sp ", context->uc_mcontext.sp);
    framePointer = context->uc_mcontext.regs[29];
#elif defined(__arm__)
    writeAddress(fd, "  pc ", context->uc_mcontext.arm_pc);
    writeAddress(fd, "  lr ", context->uc_mcontext.arm_lr);
    writeAddress(fd, "  sp ", context->uc_mcontext.arm_sp);
#elif defined(__x86_64__)
    writeAddress(fd, "  rip ", context->uc_mcontext.gregs[REG_RIP]);
    writeAddress(fd, "  rsp ", context->uc_mcontext.gregs[REG_RSP]);
#endif

    writeString(fd, "\nBacktrace (unwinder):\n");
    UnwindState state { fd, 0 };
    _Unwind_Backtrace(unwindCallback, &state);

#if defined(__aarch64__)
    // Frame pointer chain, starting from the interrupted code: works even when the unwinder can't step through the
    // signal frame. Every read is checked, so a corrupted chain just ends the walk.
    writeString(fd, "\nBacktrace (frame pointer):\n");
    for (int i = 0; i < kMaxFrames && framePointer != 0 && (framePointer & 7) == 0; ++i)
    {
        uintptr_t record[2];
        if (!safeRead(framePointer, record, sizeof(record)))
            break;

        // Strip pointer authentication bits from the return address
        uintptr_t returnAddress = record[1] & 0x0000FFFFFFFFFFFFULL;
        if (returnAddress == 0)
            break;

        writeString(fd, "  #");
        writeDecimal(fd, i);
        writeAddress(fd, " ", returnAddress);

        if (record[0] <= framePointer)
            break;
        framePointer = record[0];
    }
#endif
    writeString(fd, "\n");
}

void signalHandler(int signal, siginfo_t* info, void* context)
{
    if (!handling.exchange(true) && reportPath[0] != 0)
    {
        int fd = open(reportPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd >= 0)
        {
            writeReport(fd, signal, info, context);
            close(fd);
        }
    }

    // Give the signal back to the previous handler (debuggerd), so the system still records the crash as usual
    for (int i = 0; i < kSignalCount; ++i)
    {
        if (kSignals[i] == signal)
            sigaction(signal, &previousActions[i], nullptr);
    }
    syscall(SYS_rt_tgsigqueueinfo, getpid(), gettid(), signal, info);
}

}

void installCrashHandler(const char* path)
{
    strncpy(reportPath, path, sizeof(reportPath) - 1);
    if (installed.exchange(true))
        return;

    struct sigaction action {};
    action.sa_sigaction = signalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < kSignalCount; ++i)
        sigaction(kSignals[i], &action, &previousActions[i]);
}

void setBreadcrumb(const char* breadcrumb)
{
    currentBreadcrumb.store(breadcrumb, std::memory_order_relaxed);
}

}

extern "C"
JNIEXPORT void JNICALL
Java_me_magnum_melonds_MelonEmulator_installCrashHandler(JNIEnv* env, jobject thiz, jstring reportPath)
{
    const char* path = env->GetStringUTFChars(reportPath, nullptr);
    if (path)
    {
        MelonDSAndroid::Debug::installCrashHandler(path);
        env->ReleaseStringUTFChars(reportPath, path);
    }
}
