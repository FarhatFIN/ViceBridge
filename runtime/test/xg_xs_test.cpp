// Exercise the registered XG exports and measured vtable slots with synthetic linear texture data.
#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "shims.h"
#include "xg_xs_linear.h"

namespace {
int checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", label); }
}
using Create = HRESULT(WINAPI*)(const D3D12_RESOURCE_DESC*, void**);
using Suggest = HRESULT(WINAPI*)(const D3D12_RESOURCE_DESC*, uint32_t, uint32_t*);
using Ref = ULONG(WINAPI*)(void*);
using Layout = HRESULT(WINAPI*)(void*, rt::xg_linear::ResourceLayout*);
using Copy = HRESULT(WINAPI*)(void*, void*, uint32_t, uint32_t, const void*, uint32_t, uint32_t);
using Tiling = HRESULT(WINAPI*)(void*, void*, void*, void*, void*, uint32_t, void*);
}

int main() {
    auto create = reinterpret_cast<Create>(rt::shims::find("xg_xs.dll", "XGCreateTextureComputer"));
    auto suggest = reinterpret_cast<Suggest>(rt::shims::find("xg_xs.dll", "XGSuggestSwizzleMode"));
    check(create && suggest && rt::shims::owns_dll("xg_xs.dll"), "both exports are registered");
    if (!create || !suggest) return 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 7; desc.Height = 5; desc.DepthOrArraySize = 2; desc.MipLevels = 3;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Layout = static_cast<D3D12_TEXTURE_LAYOUT>(rt::xg_linear::kExplicitLinear);
    uint32_t mode = 0xdeadbeef;
    SetEnvironmentVariableA("LEONIDA_XG_LINEAR", nullptr);
    void* disabled = reinterpret_cast<void*>(uintptr_t(1));
    check(create(&desc, &disabled) == E_NOTIMPL && !disabled,
          "experimental model defaults to no object");
    check(suggest(&desc, 0, &mode) == E_NOTIMPL && mode == 0xdeadbeef,
          "experimental suggestion defaults to no output mutation");
    check(SetEnvironmentVariableA("LEONIDA_XG_LINEAR", "1") != FALSE, "enable the experimental model for synthetic tests");
    check(suggest(&desc, 0, &mode) == S_OK && mode == 0x100, "explicit linear layout is preserved");
    check(suggest(&desc, 0, nullptr) == E_POINTER, "suggest validates output");
    check(suggest(nullptr, 0, &mode) == E_INVALIDARG, "suggest validates descriptor");
    mode = 0xdeadbeef;
    check(suggest(&desc, 1, &mode) == E_INVALIDARG && mode == 0xdeadbeef, "reserved argument fails without output");
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    check(suggest(&desc, 0, &mode) == E_NOTIMPL && mode == 0xdeadbeef,
          "unknown layout does not silently become linear");
    void* computer = reinterpret_cast<void*>(uintptr_t(1));
    check(create(&desc, &computer) == E_NOTIMPL && !computer, "unsupported request has no fabricated object");
    check(create(nullptr, &computer) == E_INVALIDARG && !computer, "create validates descriptor");
    check(create(&desc, nullptr) == E_POINTER, "create validates output pointer");
    desc.Layout = static_cast<D3D12_TEXTURE_LAYOUT>(0x100);
    check(create(&desc, &computer) == S_OK && computer, "explicit linear request creates an owned object");
    if (!computer) return 1;
    auto** table = *static_cast<void***>(computer);
    check(reinterpret_cast<Ref>(table[0])(computer) == 2, "slot 0 retains object");
    check(reinterpret_cast<Ref>(table[1])(computer) == 1, "slot 1 drops one reference");
    struct Output { rt::xg_linear::ResourceLayout layout; uint64_t guard; } output{};
    output.guard = 0x123456789abcdef0ull;
    check(reinterpret_cast<Layout>(table[2])(computer, &output.layout) == S_OK &&
              output.layout.planes[0].mips[1].offset_bytes == 3072 && output.layout.planes[0].mips[1].row_pitch == 256,
          "slot 2 returns the bounded linear layout");
    check(output.guard == 0x123456789abcdef0ull, "layout write does not exceed measured layout span");
    check(reinterpret_cast<Layout>(table[2])(computer, nullptr) == E_POINTER, "layout validates output");
    std::array<uint8_t, 24> source{};
    for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i + 1);
    std::vector<uint8_t> destination(65536, 0xcc);
    auto copy = reinterpret_cast<Copy>(table[8]);
    check(copy(computer, destination.data(), 0, 4, source.data(), 12, 0) == S_OK &&
              std::memcmp(destination.data() + 3584, source.data(), 12) == 0 &&
              std::memcmp(destination.data() + 3584 + 256, source.data() + 12, 12) == 0,
          "slot 8 copies the requested subresource");
    const auto before = destination;
    check(copy(computer, destination.data(), 0, 4, source.data(), 11, 0) == E_INVALIDARG && destination == before,
          "invalid pitch fails before writing");
    check(copy(computer, destination.data(), 0, 4, destination.data() + 3584, 12, 0) == E_INVALIDARG &&
              destination == before, "overlap fails before writing");
    check(copy(computer, nullptr, 0, 0, source.data(), 12, 0) == E_POINTER, "copy rejects null mapping");
    void* inaccessible = VirtualAlloc(nullptr, 65536, MEM_RESERVE, PAGE_NOACCESS);
    check(inaccessible != nullptr, "reserve an inaccessible synthetic destination");
    if (inaccessible) {
        check(copy(computer, inaccessible, 0, 4, source.data(), 12, 0) == E_INVALIDARG,
              "invalid destination access is caught without ending the host");
        VirtualFree(inaccessible, 0, MEM_RELEASE);
    }
    uint64_t tiling = 0x123456789abcdef0ull;
    check(reinterpret_cast<Tiling>(table[10])(computer, &tiling, &tiling, &tiling, &tiling, 0, &tiling) == E_NOTIMPL &&
              tiling == 0x123456789abcdef0ull, "reserved-resource tiling is unsupported without output mutation");
    check(reinterpret_cast<Ref>(table[1])(computer) == 0, "last Release destroys the object");
    SetEnvironmentVariableA("LEONIDA_XG_LINEAR", nullptr);
    std::printf("xg_xs: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
