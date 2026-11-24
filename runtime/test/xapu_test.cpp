// Synthetic tests of connection memory, explicit unsupported commands, and stale handles; no DSP or game execution.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>

#include "shims.h"

namespace {

// The old connection object held one pointer. Reuse its storage deterministically only during a connect call;
// otherwise the CRT allocator's quarantine can keep a pointer-handle bug hidden. This fixture never returns the
// slot twice while it is live and never intercepts VirtualAlloc's separate shared buffer.
alignas(std::max_align_t) unsigned char g_connection_slot[sizeof(void*)];
bool g_reuse_connection_storage = false;
bool g_connection_slot_live = false;
unsigned g_connection_slot_allocations = 0;

struct ReuseConnectionStorage {
    ReuseConnectionStorage() { g_reuse_connection_storage = true; }
    ~ReuseConnectionStorage() { g_reuse_connection_storage = false; }
};

}  // namespace

void* operator new(std::size_t size) {
    if (g_reuse_connection_storage && size == sizeof(void*) && !g_connection_slot_live) {
        g_connection_slot_live = true;
        ++g_connection_slot_allocations;
        return g_connection_slot;
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept {
    if (p == g_connection_slot) g_connection_slot_live = false;
    else std::free(p);
}

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

namespace {

using XApuConnectFn = HRESULT(__stdcall*)(void*, void*, void**);
using XApuDisconnectFn = HRESULT(__stdcall*)(void*);
using XApuEnqueueCommandFn = HRESULT(__stdcall*)(void*, const uint32_t*, uint64_t);
using XApuGetResultCountFn = uint32_t(__stdcall*)(void*, uint64_t);
using XApuDequeueResultFn = HRESULT(__stdcall*)(void*, void*);

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}

template <typename T>
T get(const char* dll, const char* name) {
    void* fn = rt::shims::find(dll, name);
    check(fn != nullptr, name);
    return reinterpret_cast<T>(fn);
}

}  // namespace

int main() {
    std::printf("xapu_test: connection memory and explicit unsupported processing\n");
    const auto connect = get<XApuConnectFn>("xapu.dll", "XApuConnect");
    const auto disconnect = get<XApuDisconnectFn>("xapu.dll", "XApuDisconnect");
    const auto enqueue = get<XApuEnqueueCommandFn>("xapu.dll", "XApuEnqueueCommand");
    const auto count = get<XApuGetResultCountFn>("xapu.dll", "XApuGetResultCount");
    const auto dequeue = get<XApuDequeueResultFn>("xapu.dll", "XApuDequeueResult");
    check(rt::shims::owns_dll("xapu.dll"), "the shim owns xapu.dll");
    if (g_failures) return 1;

    // XApuConnect(out24*, out16*, handle*), success read as SUCCEEDED.
    uint8_t out24[24];
    uint64_t out16[2] = {};
    std::memset(out24, 0xcc, sizeof out24);
    void* apu = nullptr;
    check(SUCCEEDED(connect(out24, out16, &apu)) && apu, "XApuConnect succeeds with a handle");
    check(out24[0] == 0 && out24[23] == 0, "XApuConnect zeroes the 24-byte output");
    auto* base = reinterpret_cast<uint8_t*>(out16[0]);
    check(base != nullptr && out16[1] >= 0x14000 + 128 * 0x1E00, "XApuConnect returns the shared buffer and its size");
    if (base) {
        const size_t last_output = 0x14000 + 127 * 0x1E00 + 0x1E00 - 1;  // the end of slot 127's output
        check(base[0] == 0 && base[last_output] == 0, "the shared buffer is zeroed");
        base[127 * 0x280] = 1;
        base[last_output] = 1;
        check(base[127 * 0x280] == 1 && base[last_output] == 1, "the shared buffer is writable at slot 127");
    }
    check(connect(out24, out16, nullptr) == E_INVALIDARG, "XApuConnect without a handle pointer fails");

    // Command types 1, 2 and 3 as the title builds them; the third argument is 0 at every site.
    for (uint32_t type = 1; type <= 3; ++type) {
        uint32_t command[16] = {type, 5};
        check(enqueue(apu, command, 0) == E_NOTIMPL, "XApuEnqueueCommand explicitly refuses unsupported processing");
    }
    check(count(apu, 0) == 0, "XApuGetResultCount is 0");
    uint8_t result[0x50] = {};
    check(dequeue(apu, result) == E_PENDING, "empty queue reports E_PENDING");
    check(result[0] == 0 && result[sizeof result - 1] == 0, "empty dequeue preserves output");
    check(enqueue(apu, nullptr, 0) == E_INVALIDARG, "null command is rejected");
    check(dequeue(apu, nullptr) == E_POINTER, "null result is rejected");
    check(disconnect(apu) == S_OK, "XApuDisconnect succeeds");

    check(disconnect(apu) == E_HANDLE, "double disconnect is rejected");
    uint32_t invalid_command[2] = {1, 0};
    check(enqueue(apu, invalid_command, 0) == E_HANDLE, "stale handle cannot submit");
    check(dequeue(apu, result) == E_HANDLE, "stale handle cannot dequeue");

    // An allocator may return a just-freed connection's address again. Every retired handle must stay invalid,
    // including while another connection is alive and owns writable shared memory.
    std::set<void*> retired{apu};
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        void* next = nullptr;
        uint64_t next_info[2]{};
        HRESULT connected;
        {
            ReuseConnectionStorage reuse;
            connected = connect(out24, next_info, &next);
        }
        check(connected == S_OK && next, "reconnect returns a live handle");
        if (!next) break;
        check(!retired.count(next), "a reconnect never reuses a retired handle");
        auto* next_buffer = reinterpret_cast<uint8_t*>(next_info[0]);
        if (next_buffer) next_buffer[0] = 0xA5;
        for (void* stale : retired) {
            check(enqueue(stale, invalid_command, 0) == E_HANDLE, "retired handle cannot submit on a new connection");
            check(dequeue(stale, result) == E_HANDLE, "retired handle cannot dequeue on a new connection");
        }
        check(enqueue(next, invalid_command, 0) == E_NOTIMPL, "the new handle still reaches unsupported processing");
        check(dequeue(next, result) == E_PENDING, "the new handle still reports an empty queue");
        check(next_buffer && next_buffer[0] == 0xA5, "stale handle checks preserve the new connection's memory");
        check(disconnect(next) == S_OK, "the new connection can disconnect");
        retired.insert(next);
    }

    std::printf("connection allocator fixture: %u reused-slot allocations\n", g_connection_slot_allocations);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
