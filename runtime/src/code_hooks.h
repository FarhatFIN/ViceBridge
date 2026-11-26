// Hooks on a function of the loaded game image, by redirecting its direct callers (docs/modding.md, "Day one").
//
// Patching inside a function needs an instruction decoder to move its prologue. Redirecting the callers does not:
// every `call rel32` (E8) and `jmp rel32` (E9) in the image's executable sections whose target is the function is
// pointed at a thunk instead. The thunk saves the argument registers (rcx, rdx, r8, r9, xmm0-3), passes one 32-bit
// argument through a C++ function, restores the rest and jumps to the original. A call through a pointer (vtable,
// import slot) is not redirected, which suits RAGE's archetype lookup: a static function called directly.
//
// A byte pattern can look like E8/E9 + rel32 inside another instruction; it is only rewritten when its target is
// exactly the hooked function, so a false match must also point there. x64 Windows only.
#pragma once

#include <windows.h>
#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace leonida::hooks {

using RemapFn = uint32_t (*)(uint32_t);

// Executable memory within +-2 GiB of `close_to`, so a rel32 branch from the image reaches it. nullptr if none is free.
inline uint8_t* alloc_near(const void* close_to, std::size_t size) {
    if (!close_to || size == 0) return nullptr;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t center = reinterpret_cast<uintptr_t>(close_to) & ~(gran - 1);
    const uintptr_t span = 0x70000000;  // stay well inside the rel32 range from anywhere in a large image
    for (uintptr_t delta = gran; delta < span; delta += gran) {
        for (int side = 0; side < 2; ++side) {
            if (side == 0 && center < delta) continue;
            if (side == 1 && UINTPTR_MAX - center < delta) continue;
            const uintptr_t at = side ? center + delta : center - delta;
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

// The thunk: argument `arg` (0 = rcx, 1 = rdx; the low 32 bits) goes through `fn`, then the original runs with every
// other argument register as the caller left it. Stack: the caller's call leaves rsp = 8 mod 16; four pushes and
// 0x68 bytes make it 16-aligned for the call to `fn`, with its 32-byte shadow space and room for xmm0-3.
inline uint8_t* make_remap_thunk(const void* close_to, int arg, RemapFn fn, const void* original) {
    if (!close_to || !fn || !original || (arg != 0 && arg != 1)) return nullptr;
    uint8_t* t = alloc_near(close_to, 4096);
    if (!t) return nullptr;
    uint8_t* p = t;
    auto put = [&](std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) *p++ = b; };
    auto put64 = [&](uint64_t v) { std::memcpy(p, &v, 8); p += 8; };
    put({0x51, 0x52, 0x41, 0x50, 0x41, 0x51});        // push rcx, rdx, r8, r9
    put({0x48, 0x83, 0xEC, 0x68});                    // sub rsp, 0x68
    put({0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20});        // movdqu [rsp+0x20], xmm0
    put({0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30});        // movdqu [rsp+0x30], xmm1
    put({0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40});        // movdqu [rsp+0x40], xmm2
    put({0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50});        // movdqu [rsp+0x50], xmm3
    if (arg == 1) put({0x89, 0xD1});                   // mov ecx, edx
    put({0x48, 0xB8}); put64(reinterpret_cast<uint64_t>(fn));         // mov rax, fn
    put({0xFF, 0xD0});                                 // call rax
    put({0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20});        // movdqu xmm0, [rsp+0x20]
    put({0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30});        // movdqu xmm1, [rsp+0x30]
    put({0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40});        // movdqu xmm2, [rsp+0x40]
    put({0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50});        // movdqu xmm3, [rsp+0x50]
    put({0x48, 0x83, 0xC4, 0x68});                    // add rsp, 0x68
    put({0x41, 0x59, 0x41, 0x58, 0x5A, 0x59});        // pop r9, r8, rdx, rcx
    put({0x89, static_cast<uint8_t>(arg == 0 ? 0xC1 : 0xC2)});         // mov ecx|edx, eax (the remapped value)
    put({0x48, 0xB8}); put64(reinterpret_cast<uint64_t>(original));   // mov rax, original
    put({0xFF, 0xE0});                                 // jmp rax
    DWORD old = 0;
    if (!VirtualProtect(t, 4096, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(t, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), t, p - t);
    return t;
}

using ResultFn = uint32_t (*)(uint32_t result, uint32_t arg0);

// A thunk that runs the original and then passes (its 32-bit result, its first argument) through `fn`, returning
// what `fn` returns: for a getter such as "the view mode of context N". Only for functions with at most four
// register arguments (a stack argument would be one slot off) and an integer result.
// Stack: rsp = 8 mod 16 at entry; one push and 0x20 bytes of shadow space make it 16-aligned for both calls.
inline uint8_t* make_result_thunk(const void* close_to, ResultFn fn, const void* original) {
    if (!close_to || !fn || !original) return nullptr;
    uint8_t* t = alloc_near(close_to, 4096);
    if (!t) return nullptr;
    uint8_t* p = t;
    auto put = [&](std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) *p++ = b; };
    auto put64 = [&](uint64_t v) { std::memcpy(p, &v, 8); p += 8; };
    put({0x51});                                       // push rcx (the first argument, for fn)
    put({0x48, 0x83, 0xEC, 0x20});                     // sub rsp, 0x20
    put({0x48, 0xB8}); put64(reinterpret_cast<uint64_t>(original));   // mov rax, original
    put({0xFF, 0xD0});                                 // call rax (rcx, rdx, r8, r9 still the caller's)
    put({0x48, 0x8B, 0x54, 0x24, 0x20});               // mov rdx, [rsp+0x20] (the saved first argument)
    put({0x89, 0xC1});                                 // mov ecx, eax (the original's result)
    put({0x48, 0xB8}); put64(reinterpret_cast<uint64_t>(fn));         // mov rax, fn
    put({0xFF, 0xD0});                                 // call rax
    put({0x48, 0x83, 0xC4, 0x20});                     // add rsp, 0x20
    put({0x59, 0xC3});                                 // pop rcx ; ret
    DWORD old = 0;
    if (!VirtualProtect(t, 4096, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(t, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), t, p - t);
    return t;
}

// Points every E8/E9 rel32 in [begin, begin + size) whose target is `target` at `thunk`. The range must be writable.
// Returns how many were rewritten.
inline std::size_t redirect_direct_branches(uint8_t* begin, std::size_t size, const uint8_t* target,
                                            const uint8_t* thunk) {
    if (!begin || !target || !thunk || size < 5) return 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 5 <= size; ++i) {
        if (begin[i] != 0xE8 && begin[i] != 0xE9) continue;
        int32_t rel;
        std::memcpy(&rel, begin + i + 1, 4);
        const uint8_t* next = begin + i + 5;
        const uintptr_t next_addr = reinterpret_cast<uintptr_t>(next);
        const uintptr_t dst_addr = next_addr + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
        if (dst_addr != reinterpret_cast<uintptr_t>(target)) continue;
        const intptr_t to = static_cast<intptr_t>(reinterpret_cast<uintptr_t>(thunk) - next_addr);
        if (to < INT32_MIN || to > INT32_MAX) continue;
        const int32_t rel2 = static_cast<int32_t>(to);
        std::memcpy(begin + i + 1, &rel2, 4);
        ++n;
        i += 4;
    }
    return n;
}

struct HookResult {
    bool ok = false;
    std::size_t redirected = 0;
    const char* error = "";
};

// Hooks the function at `target_rva` of the mapped image at `base`: one thunk, then every direct call or jump to it
// in the image's executable sections. Runs before any image code (main.cpp), so no thread is executing the bytes.
// Validates `target_rva` as code of the mapped image at `base`, builds the thunk with `make(target)` and points the
// image's direct calls and jumps to the target at it.
template <class MakeThunk>
inline HookResult redirect_image_calls(uint8_t* base, uint32_t target_rva, MakeThunk make) {
    HookResult r;
    if (!base) return r.error = "not a mapped PE image", r;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)))
        return r.error = "not a mapped PE image", r;
    const uint64_t lfanew = static_cast<uint64_t>(dos->e_lfanew);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return r.error = "not a mapped PE64 image", r;
    const uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    const uint64_t sec_table_end =
        lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
        static_cast<uint64_t>(nt->FileHeader.SizeOfOptionalHeader) +
        static_cast<uint64_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (image_size == 0 || sec_table_end > image_size)
        return r.error = "not a mapped PE64 image", r;
    if (target_rva >= image_size) return r.error = "the RVA is outside the image", r;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    // Match pe_loader.cpp's effective section span (VirtualSize, falling back to SizeOfRawData when 0), clamped to SizeOfImage.
    auto exec_section_span = [&](const IMAGE_SECTION_HEADER& s, uint64_t& va, uint64_t& len) -> bool {
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) return false;
        va = s.VirtualAddress;
        const uint64_t raw = s.Misc.VirtualSize ? static_cast<uint64_t>(s.Misc.VirtualSize)
                                                : static_cast<uint64_t>(s.SizeOfRawData);
        if (raw == 0 || va >= image_size) return false;
        len = raw > image_size - va ? image_size - va : raw;
        return len > 0;
    };
    bool in_code = false;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uint64_t va = 0, len = 0;
        if (exec_section_span(sec[i], va, len) && target_rva >= va && target_rva < va + len)
            in_code = true;
    }
    if (!in_code) return r.error = "the RVA is not in an executable section", r;
    uint8_t* target = base + target_rva;
    uint8_t* thunk = make(target);
    if (!thunk) return r.error = "no memory within 2 GiB of the image for the thunk", r;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uint64_t va = 0, len = 0;
        if (!exec_section_span(sec[i], va, len)) continue;
        uint8_t* begin = base + va;
        const std::size_t size = static_cast<std::size_t>(len);
        DWORD old = 0;
        if (!VirtualProtect(begin, size, PAGE_EXECUTE_READWRITE, &old)) {
            if (r.redirected == 0) VirtualFree(thunk, 0, MEM_RELEASE);
            return r.error = "VirtualProtect failed", r;
        }
        r.redirected += redirect_direct_branches(begin, size, target, thunk);
        DWORD ignored = 0;
        VirtualProtect(begin, size, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), begin, size);
    }
    r.ok = true;
    return r;
}

// The argument-remapping hook (archetype lookup): argument `arg` of every direct call goes through `fn`.
inline HookResult hook_image_calls(uint8_t* base, uint32_t target_rva, RemapFn fn, int arg) {
    if (!fn) return HookResult{false, 0, "hook callback is null"};
    if (arg != 0 && arg != 1) return HookResult{false, 0, "only argument 0 or 1 can be remapped"};
    return redirect_image_calls(base, target_rva,
                                [&](uint8_t* target) { return make_remap_thunk(target, arg, fn, target); });
}

// The result-override hook (view-mode getter): every direct call's result goes through `fn` with the first argument.
inline HookResult hook_image_results(uint8_t* base, uint32_t target_rva, ResultFn fn) {
    if (!fn) return HookResult{false, 0, "hook callback is null"};
    return redirect_image_calls(base, target_rva,
                                [&](uint8_t* target) { return make_result_thunk(target, fn, target); });
}

// Rewrite an explicitly identified x64 TLS-vector read. Both instructions are
// known. The replacement preserves flags, the continuation address, and register effects of the original pair.
inline const char* redirect_tls_zero_read(uint8_t* site, std::size_t remaining, uint32_t slot) {
    constexpr uint8_t original[] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0, 0, 0,
                                    0x48, 0x8B, 0x00};  // mov rax, gs:[58h]; mov rax, [rax]
    if (!site || remaining < sizeof(original) || slot > INT32_MAX / 8)
        return "invalid TLS rewrite range or slot";
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(site, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect != PAGE_EXECUTE_READ && region.Protect != PAGE_EXECUTE_READWRITE) ||
        region.RegionSize - (reinterpret_cast<uintptr_t>(site) - reinterpret_cast<uintptr_t>(region.BaseAddress))
            < sizeof(original))
        return "TLS rewrite site is not readable executable memory";
    const bool extended_base = (site[1] & 0x04) != 0;
    const uint8_t base_low = (site[3] >> 3) & 7;
    const uint8_t reg = base_low | (extended_base ? 8 : 0);
    // A RIP-relative MOV can intervene, optionally preceded by a register copy.
    constexpr std::size_t rip_load_size = 7, register_move_size = 3;
    constexpr uint8_t stack_saves[] = {0x48, 0x89, 0x54, 0x24, 0x40, 0x44, 0x88, 0x4C, 0x24, 0x2F};
    constexpr uint8_t widen_index[] = {0x44, 0x0F, 0xB7, 0xE3};
    constexpr std::size_t scheduled_gap = sizeof(stack_saves) + rip_load_size + sizeof(widen_index);
    constexpr std::size_t max_gap = scheduled_gap + register_move_size;
    const auto readable = region.RegionSize -
        (reinterpret_cast<uintptr_t>(site) - reinterpret_cast<uintptr_t>(region.BaseAddress));
    const auto available = remaining < readable ? remaining : readable;
    const uint8_t move_dest = site[11] & 7;
    const bool register_move = available >= sizeof(original) + rip_load_size + register_move_size &&
        site[9] == 0x48 && site[10] == 0x89 && (site[11] & 0xC0) == 0xC0 &&
        move_dest != reg && move_dest != 4 && move_dest != 5;
    // Recognize the scheduled variant only as a complete, straight-line sequence.
    std::size_t stack_prefix = 0;
    if (reg == 0 && available >= sizeof(original) + 5 + rip_load_size + sizeof(widen_index) &&
        (std::memcmp(site + 9, stack_saves, 5) == 0 ||
         std::memcmp(site + 9, "\x44\x88\x44\x24\x2f", 5) == 0)) {
        stack_prefix = 5;
        if (available >= sizeof(original) + scheduled_gap &&
            std::memcmp(site + 14, stack_saves + 5, sizeof(stack_saves) - 5) == 0)
            stack_prefix = sizeof(stack_saves);
    }
    const auto* widening = stack_prefix ? site + 9 + stack_prefix + rip_load_size : nullptr;
    const bool scheduled = stack_prefix != 0 &&
        std::memcmp(widening, widen_index, sizeof(widen_index) - 1) == 0 && (widening[3] & 0xC0) == 0xC0;
    const std::size_t prefix = scheduled ? stack_prefix : register_move ? register_move_size : 0;
    const std::size_t rip_offset = 9 + prefix;
    const std::size_t after_rip = rip_offset + rip_load_size;
    const bool trailing_copy = !scheduled && !register_move &&
        available >= sizeof(original) + rip_load_size + register_move_size &&
        site[after_rip] == 0x48 && site[after_rip + 1] == 0x89 &&
        (site[after_rip + 2] & 0xC0) == 0xC0 &&
        (site[after_rip + 2] & 7) != reg && (site[after_rip + 2] & 7) != 4 &&
        (site[after_rip + 2] & 7) != 5;
    const auto widening_end = after_rip + sizeof(widen_index);
    const bool scheduled_copy = scheduled &&
        available >= sizeof(original) + stack_prefix + rip_load_size + sizeof(widen_index) + register_move_size &&
        site[widening_end] == 0x49 && site[widening_end + 1] == 0x89 &&
        (site[widening_end + 2] & 0xC0) == 0xC0;
    const std::size_t suffix = scheduled ? sizeof(widen_index) + (scheduled_copy ? register_move_size : 0) :
                              trailing_copy ? register_move_size : 0;
    const uint8_t rip_dest = (site[11 + prefix] >> 3) & 7;
    const bool rip_load = available >= sizeof(original) + rip_load_size + prefix + suffix &&
        site[rip_offset] == 0x48 && (site[rip_offset + 1] == 0x8B || site[rip_offset + 1] == 0x8D) &&
        (site[rip_offset + 2] & 0xC7) == 0x05 && rip_dest != reg && rip_dest != 4 && rip_dest != 5;
    constexpr std::size_t immediate_size = 5;
    const auto immediate_dest = site[9] & 7;
    const bool immediate_move = !rip_load && available >= sizeof(original) + immediate_size &&
        (site[9] & 0xF8) == 0xB8 && immediate_dest != reg && immediate_dest != 4 && immediate_dest != 5;
    constexpr std::size_t lea_size = 4;
    const uint8_t lea_dest = (site[11] >> 3) & 7;
    const bool lea = !rip_load && available >= sizeof(original) + lea_size &&
        site[9] == 0x48 && site[10] == 0x8D && (site[11] & 0xC0) == 0x40 &&
        (site[11] & 7) != 4 && lea_dest != reg && lea_dest != 4 && lea_dest != 5;
    // ADD r64, imm8 can be scheduled before the dereference. Its flags and
    // register effects are preserved by replaying it in the trampoline.
    constexpr std::size_t add_size = 4;
    const uint8_t add_dest = (site[11] & 7) | ((site[9] & 1) ? 8 : 0);
    const bool add = !rip_load && available >= sizeof(original) + add_size &&
        (site[9] == 0x48 || site[9] == 0x49) && site[10] == 0x83 &&
        (site[11] & 0xF8) == 0xC0 && add_dest != reg && add_dest != 4 && add_dest != 5;
    const std::size_t gap = rip_load ? rip_load_size + prefix + suffix :
                            immediate_move ? immediate_size : lea ? lea_size : add ? add_size : 0;
    const std::size_t span = sizeof(original) + gap;
    const uint8_t destination = (site[11 + gap] >> 3) & 7;
    uint8_t expected[sizeof(original)];
    std::memcpy(expected, original, sizeof(expected));
    expected[1] |= extended_base ? 0x04 : 0;
    expected[3] |= base_low << 3;
    // The second instruction must address the same vector register.
    expected[9] |= (site[9 + gap] & 0x04) | (extended_base ? 0x01 : 0);
    expected[11] = (destination << 3) | base_low;
    const bool stack_or_frame_destination = !(expected[9] & 0x04) && (destination == 4 || destination == 5);
    if (base_low == 4 || base_low == 5 || stack_or_frame_destination || std::memcmp(site, expected, 9) != 0 ||
        std::memcmp(site + 9 + gap, expected + 9, 3) != 0)
        return "TLS rewrite instruction does not match";
    if (slot == 0) return nullptr;

    // MOV RAX,moffs32 saves the byte needed for the second instruction's displacement.
    uint8_t patch[sizeof(original) + max_gap] = {0x65, 0x67, 0x48, 0xA1, 0x58, 0, 0, 0,
                       expected[9], 0x8B, static_cast<uint8_t>(0x40 | (destination << 3)), static_cast<uint8_t>(slot * 8)};
    uint8_t* thunk = nullptr;
    if (gap != 0 || reg != 0 || slot > INT8_MAX / 8) {
        // Use a trampoline when the in-place form needs a longer displacement
        // or the TLS vector is loaded into a register other than RAX.
        thunk = alloc_near(site, 4096);
        if (!thunk) return "cannot allocate a nearby TLS trampoline";
        uint8_t code[21 + max_gap]{};
        std::memcpy(code, site, span);
        if (rip_load) {
            int32_t old_displacement;
            std::memcpy(&old_displacement, site + rip_offset + 3, sizeof(old_displacement));
            const auto adjusted = static_cast<int64_t>(old_displacement) +
                reinterpret_cast<intptr_t>(site) - reinterpret_cast<intptr_t>(thunk);
            if (adjusted < INT32_MIN || adjusted > INT32_MAX) {
                VirtualFree(thunk, 0, MEM_RELEASE);
                return "RIP-relative TLS operand out of range";
            }
            const auto relocated = static_cast<int32_t>(adjusted);
            std::memcpy(code + rip_offset + 3, &relocated, sizeof(relocated));
        }
        code[11 + gap] |= 0x80;  // [register + disp32]
        const auto displacement = static_cast<int32_t>(slot * 8);
        std::memcpy(code + 12 + gap, &displacement, sizeof(displacement));
        code[16 + gap] = 0xE9;
        const auto back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site + span) -
                                                reinterpret_cast<intptr_t>(thunk + 21 + gap));
        std::memcpy(code + 17 + gap, &back, sizeof(back));
        std::memcpy(thunk, code, 21 + gap);
        DWORD ignored = 0;
        if (!VirtualProtect(thunk, 4096, PAGE_EXECUTE_READ, &ignored) ||
            !FlushInstructionCache(GetCurrentProcess(), thunk, 21 + gap)) {
            VirtualFree(thunk, 0, MEM_RELEASE);
            return "cannot finalize TLS trampoline";
        }
        std::memset(patch, 0x90, span);
        patch[0] = 0xE9;
        const auto forward = static_cast<int32_t>(reinterpret_cast<intptr_t>(thunk) -
                                                   reinterpret_cast<intptr_t>(site + 5));
        std::memcpy(patch + 1, &forward, sizeof(forward));
    }
    DWORD old = 0;
    if (!VirtualProtect(site, span, PAGE_EXECUTE_READWRITE, &old)) {
        if (thunk) VirtualFree(thunk, 0, MEM_RELEASE);
        return "cannot change TLS rewrite site protection";
    }
    std::memcpy(site, patch, span);
    DWORD ignored = 0;
    if (!VirtualProtect(site, span, old, &ignored) ||
        !FlushInstructionCache(GetCurrentProcess(), site, span))
        return "cannot finalize TLS rewrite";
    return nullptr;
}

// Replace the verified CLWB [r9] form without moving the continuation.
inline const char* adapt_clwb_r9(uint8_t* site, std::size_t remaining) {
    constexpr uint8_t clwb[]{0x66, 0x41, 0x0f, 0xae, 0x31};
    MEMORY_BASIC_INFORMATION region{};
    if (!site || remaining < sizeof(clwb) || !VirtualQuery(site, &region, sizeof(region)) ||
        region.State != MEM_COMMIT ||
        (region.Protect != PAGE_EXECUTE_READ && region.Protect != PAGE_EXECUTE_READWRITE) ||
        region.RegionSize - (reinterpret_cast<uintptr_t>(site) - reinterpret_cast<uintptr_t>(region.BaseAddress))
            < sizeof(clwb) || std::memcmp(site, clwb, sizeof(clwb)) != 0)
        return "expected executable CLWB [r9]";

    int cpu[4]{};
    __cpuid(cpu, 0);
    bool optimized = false;
    if (cpu[0] >= 7) {
        __cpuidex(cpu, 7, 0);
        if (cpu[1] & (1 << 24)) return nullptr;  // CLWB
        optimized = (cpu[1] & (1 << 23)) != 0;  // CLFLUSHOPT
    }
    __cpuid(cpu, 1);
    if (!optimized && !(cpu[3] & (1 << 19))) return "CPU has no supported cache-line flush";

    // CLFLUSHOPT also writes back dirty data and invalidates the line.
    // A NOP replaces its operand prefix on CPUs that only have CLFLUSH.
    DWORD protect = 0;
    if (!VirtualProtect(site, sizeof(clwb), PAGE_EXECUTE_READWRITE, &protect))
        return "cannot change cache writeback instruction protection";
    if (!optimized) site[0] = 0x90;
    site[4] = 0x39;
    DWORD ignored = 0;
    if (!VirtualProtect(site, sizeof(clwb), protect, &ignored) ||
        !FlushInstructionCache(GetCurrentProcess(), site, sizeof(clwb)))
        return "cannot finalize cache writeback rewrite";
    return nullptr;
}

}  // namespace leonida::hooks
