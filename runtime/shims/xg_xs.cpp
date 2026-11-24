// Strict host-linear subset, adapted from the XG implementation in upstream PR #8134.
// Native console swizzles, automatic swizzle selection and reserved tiling are deliberately unsupported.
#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "shims.h"
#include "xg_xs_linear.h"

namespace {

using namespace rt::xg_linear;

static_assert(sizeof(D3D12_RESOURCE_DESC) == 0x38);
static_assert(offsetof(D3D12_RESOURCE_DESC, Width) == 0x10 &&
              offsetof(D3D12_RESOURCE_DESC, Layout) == 0x2c && offsetof(D3D12_RESOURCE_DESC, Flags) == 0x30);

Description describe(const D3D12_RESOURCE_DESC& d) {
    return {static_cast<uint32_t>(d.Dimension), d.Alignment, d.Width, d.Height, d.DepthOrArraySize, d.MipLevels,
            static_cast<uint32_t>(d.Format), d.SampleDesc.Count, d.SampleDesc.Quality,
            static_cast<uint32_t>(d.Layout), static_cast<uint32_t>(d.Flags)};
}

HRESULT as_hresult(Result r) {
    return r == Result::Ok ? S_OK : r == Result::Unsupported ? E_NOTIMPL : E_INVALIDARG;
}

bool experimental_linear_enabled() {
    char value[2]{};
    const bool enabled = GetEnvironmentVariableA("LEONIDA_XG_LINEAR", value, sizeof value) == 1 && value[0] == '1';
    if (!enabled) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true, std::memory_order_relaxed))
            std::fprintf(stderr, "[rt] xg_xs: experimental host-linear layout is disabled (E_NOTIMPL); "
                                 "LEONIDA_XG_LINEAR=1 opts in; native Xbox layouts remain unsupported\n");
    }
    return enabled;
}

struct Computer {
    void* const* vtable;
    std::atomic<ULONG> refs{1};
    Texture texture;
};

ULONG WINAPI add_ref(Computer* c) { return ++c->refs; }
ULONG WINAPI release(Computer* c) {
    const ULONG refs = --c->refs;
    if (!refs) delete c;
    return refs;
}

HRESULT WINAPI get_resource_layout(Computer* c, ResourceLayout* out) {
    if (!out) return E_POINTER;
    std::memcpy(out, &c->texture.layout(), sizeof *out);
    return S_OK;
}

// The export has no buffer-length arguments; enforce the computed extents, pitch and overlap rules before any
// writes. The caller still owns valid mappings of those sizes. SEH prevents an invalid mapping from ending the host.
HRESULT guarded_copy(Computer* c, void* base, uint32_t plane, uint32_t subresource, const void* source,
                     uint32_t row_pitch, uint32_t slice_pitch) {
    if (!base || !source) return E_POINTER;
    Texture::CopyRange range{};
    const Result checked = c->texture.copy_range(plane, subresource, row_pitch, slice_pitch, range);
    if (checked != Result::Ok) return as_hresult(checked);
    const auto from = reinterpret_cast<uintptr_t>(source);
    const auto to = reinterpret_cast<uintptr_t>(base);
    if (range.source_size > UINTPTR_MAX - from || range.destination_size > UINTPTR_MAX - to) return E_INVALIDARG;
    __try {
        return as_hresult(c->texture.copy_into(
            plane, subresource, {static_cast<const std::byte*>(source), static_cast<size_t>(range.source_size)},
            row_pitch, slice_pitch, {static_cast<std::byte*>(base), static_cast<size_t>(range.destination_size)}));
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                               : EXCEPTION_CONTINUE_SEARCH) {
        return E_INVALIDARG;
    }
}

HRESULT WINAPI copy_into(Computer* c, void* base, uint32_t plane, uint32_t subresource, const void* source,
                         uint32_t row_pitch, uint32_t slice_pitch) {
    return guarded_copy(c, base, plane, subresource, source, row_pitch, slice_pitch);
}

template <size_t Slot>
HRESULT WINAPI unsupported(Computer*) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true, std::memory_order_relaxed))
        std::fprintf(stderr, "[rt] xg_xs: texture computer slot %zu is unsupported (E_NOTIMPL)\n", Slot);
    return E_NOTIMPL;
}

template <size_t... Slot>
auto make_vtable(std::index_sequence<Slot...>) {
    return std::array<void*, sizeof...(Slot)>{
        (Slot == 0 ? reinterpret_cast<void*>(&add_ref) :
         Slot == 1 ? reinterpret_cast<void*>(&release) :
         Slot == 2 ? reinterpret_cast<void*>(&get_resource_layout) :
         Slot == 8 ? reinterpret_cast<void*>(&copy_into) : reinterpret_cast<void*>(&unsupported<Slot>))...};
}
const auto kVtable = make_vtable(std::make_index_sequence<32>{});

HRESULT WINAPI XGCreateTextureComputer(const D3D12_RESOURCE_DESC* desc, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!desc) return E_INVALIDARG;
    if (!experimental_linear_enabled()) return E_NOTIMPL;
    Texture texture;
    const Result result = Texture::make(describe(*desc), texture);
    if (result != Result::Ok) return as_hresult(result);
    auto* computer = new (std::nothrow) Computer{kVtable.data()};
    if (!computer) return E_OUTOFMEMORY;
    computer->texture = texture;
    *out = computer;
    return S_OK;
}

HRESULT WINAPI XGSuggestSwizzleMode(const D3D12_RESOURCE_DESC* desc, uint32_t reserved, uint32_t* mode) {
    if (!mode) return E_POINTER;
    if (!desc || reserved != 0) return E_INVALIDARG;
    if (!experimental_linear_enabled()) return E_NOTIMPL;
    Texture checked;
    const Result valid = Texture::make(describe(*desc), checked);
    if (valid != Result::Ok) return as_hresult(valid);
    // Preserve a valid explicit linear request. Choosing linear for an unknown/tiled request changes the title's
    // memory contract and cannot be justified by the measured call sites.
    *mode = kExplicitLinear;
    return S_OK;
}

}  // namespace

SHIM_TABLE(xg_xs,
    {"xg_xs.dll", "XGCreateTextureComputer", reinterpret_cast<void*>(&XGCreateTextureComputer)},
    {"xg_xs.dll", "XGSuggestSwizzleMode", reinterpret_cast<void*>(&XGSuggestSwizzleMode)},
);
