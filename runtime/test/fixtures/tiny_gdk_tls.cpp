// A mapped dependency exercises the host lookup in its own CRT startup.
#include <windows.h>

namespace {
thread_local volatile unsigned g_dependency_tls = 40;
bool g_lookup_ok = false;

struct EarlyHostLookup {
    EarlyHostLookup() {
        if (g_dependency_tls != 40) return;
        g_dependency_tls = 41;
        HMODULE host = GetModuleHandleW(L"kernel32.dll");
        auto process_id = host ? reinterpret_cast<decltype(&GetCurrentProcessId)>(
                                     GetProcAddress(host, "GetCurrentProcessId")) : nullptr;
        g_lookup_ok = process_id && process_id() == GetCurrentProcessId() && g_dependency_tls == 41;
    }
};
EarlyHostLookup g_early_lookup;
}

extern "C" __declspec(dllexport) bool tiny_gdk_dependency_lookup_ok() { return g_lookup_ok; }

extern "C" __declspec(dllexport) unsigned tiny_gdk_dependency_tls_exchange(unsigned value) {
    const auto previous = g_dependency_tls;
    g_dependency_tls = value;
    return previous;
}
