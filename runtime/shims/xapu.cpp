// Adapted from upstream PR #6981: connection memory and five XApu exports only.
// Commands return E_NOTIMPL rather than silently succeeding. This is connection management, not DSP emulation.
// XApuConnect has three output pointers; the shared buffer's minimum layout is inferred from title uses.
// Its size (0x14000 + 128 * 0x1E00) and the second info qword being a size are hypotheses, not a public ABI.
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <string>

#include "shims.h"
#include "startup_trace.h"

namespace {

constexpr size_t kApuBufferSize = 0x14000 + 128 * 0x1E00;

struct ApuConnection {
    void* buffer;
};

std::mutex g_connections_mutex;
// Measured callers pass the handle back without dereferencing it. A deleted connection pointer can be reused by
// the allocator, making a stale handle control a later connection. Process-lifetime IDs are never reused.
std::map<uintptr_t, ApuConnection> g_connections;
uintptr_t g_next_connection = 1;

// Trace each API once with its caller without reading unsupported command structures.
void note(const char* api, uint64_t value, const void* caller) {
    if (!rt::startup_trace::enabled()) return;
    static std::mutex mutex;
    static std::set<std::pair<std::string, uint64_t>> seen;
    {
        const std::lock_guard lock(mutex);
        if (!seen.insert({api, value}).second) return;
    }
    rt::startup_trace::event(
        "audio_queue", std::string("\"api\":") + rt::startup_trace::quote(api) +
                           ",\"value\":" + rt::startup_trace::quote(rt::startup_trace::hex(value)) + ",\"caller\":" +
                           rt::startup_trace::quote(rt::startup_trace::where(reinterpret_cast<uint64_t>(caller))));
}

HRESULT __stdcall XApuConnect(void* out24, void* out16, void** handle) {
    note("XApuConnect", 0, _ReturnAddress());
    if (!handle) return E_INVALIDARG;
    *handle = nullptr;
    void* buffer = VirtualAlloc(nullptr, kApuBufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);  // zeroed
    if (!buffer) return E_OUTOFMEMORY;
    {
        std::lock_guard lock(g_connections_mutex);
        if (g_next_connection == 0) {
            VirtualFree(buffer, 0, MEM_RELEASE);
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
        }
        try {
            g_connections.emplace(g_next_connection, ApuConnection{buffer});
        } catch (const std::bad_alloc&) {
            VirtualFree(buffer, 0, MEM_RELEASE);
            return E_OUTOFMEMORY;
        }
        *handle = reinterpret_cast<void*>(g_next_connection++);
    }
    if (out24) std::memset(out24, 0, 24);
    if (out16) {
        const uint64_t info[2] = {reinterpret_cast<uint64_t>(buffer), kApuBufferSize};
        std::memcpy(out16, info, sizeof info);
    }
    return S_OK;
}

HRESULT __stdcall XApuDisconnect(void* handle) {
    note("XApuDisconnect", 0, _ReturnAddress());
    std::lock_guard lock(g_connections_mutex);
    const auto connection = g_connections.find(reinterpret_cast<uintptr_t>(handle));
    if (connection == g_connections.end()) return E_HANDLE;
    if (!VirtualFree(connection->second.buffer, 0, MEM_RELEASE)) return HRESULT_FROM_WIN32(GetLastError());
    g_connections.erase(connection);
    return S_OK;
}

HRESULT __stdcall XApuEnqueueCommand(void* handle, const uint32_t* command, uint64_t flags) {
    note("XApuEnqueueCommand", 0, _ReturnAddress());
    std::lock_guard lock(g_connections_mutex);
    if (!g_connections.count(reinterpret_cast<uintptr_t>(handle))) return E_HANDLE;
    if (!command || flags != 0) return E_INVALIDARG;
    // No DSP decoder exists: an accepted command would promise a result that cannot be produced.
    static std::once_flag warning;
    std::call_once(warning, [] { std::fprintf(stderr, "[rt] xapu: command processing unavailable (E_NOTIMPL)\n"); });
    return E_NOTIMPL;
}

uint32_t __stdcall XApuGetResultCount(void* handle, uint64_t flags) {
    note("XApuGetResultCount", 0, _ReturnAddress());
    std::lock_guard lock(g_connections_mutex);
    if (!g_connections.count(reinterpret_cast<uintptr_t>(handle))) SetLastError(ERROR_INVALID_HANDLE);
    else if (flags != 0) SetLastError(ERROR_INVALID_PARAMETER);
    else SetLastError(ERROR_SUCCESS);
    return 0;  // No command was accepted, so no result can be available.
}

HRESULT __stdcall XApuDequeueResult(void* handle, void* result) {
    note("XApuDequeueResult", 0, _ReturnAddress());
    std::lock_guard lock(g_connections_mutex);
    if (!g_connections.count(reinterpret_cast<uintptr_t>(handle))) return E_HANDLE;
    if (!result) return E_POINTER;
    return E_PENDING;  // Output remains untouched: there is no decoded result to copy.
}

}  // namespace

SHIM_TABLE(xapu,
    {"xapu.dll", "XApuConnect", reinterpret_cast<void*>(&XApuConnect)},
    {"xapu.dll", "XApuDequeueResult", reinterpret_cast<void*>(&XApuDequeueResult)},
    {"xapu.dll", "XApuDisconnect", reinterpret_cast<void*>(&XApuDisconnect)},
    {"xapu.dll", "XApuEnqueueCommand", reinterpret_cast<void*>(&XApuEnqueueCommand)},
    {"xapu.dll", "XApuGetResultCount", reinterpret_cast<void*>(&XApuGetResultCount)},
);
