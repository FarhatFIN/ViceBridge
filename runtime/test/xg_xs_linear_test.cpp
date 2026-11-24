// Synthetic host-linear footprints. No game files, Windows API, Vulkan driver or GPU are needed.
#include "xg_xs_linear.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace rt::xg_linear;

namespace {
int checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", label); }
}

Description rgba() { return {3, 0, 7, 5, 2, 3, 28, 1, 0, kExplicitLinear, 0}; }

void layouts() {
    Texture texture;
    check(Texture::make(rgba(), texture) == Result::Ok, "explicit linear RGBA8 array builds");
    const auto& layout = texture.layout();
    check(layout.size_bytes == 65536 && layout.alignment_bytes == 65536 && layout.plane_count == 1 &&
              layout.mip_levels == 3, "resource alignment and plane count");
    const auto& m = layout.planes[0].mips;
    check(m[0].row_pitch == 256 && m[0].slice_bytes == 1536 && m[0].size_bytes == 3072,
          "7x5 two-slice footprint rounds rows and slices");
    check(m[1].offset_bytes == 3072 && m[1].slice_bytes == 512 && m[1].row_pitch == 256 &&
              m[2].offset_bytes == 4096, "mip offsets retain all array slices");
    auto d = rgba(); d.mip_levels = 0;
    check(Texture::make(d, texture) == Result::Ok && texture.layout().mip_levels == 3,
          "zero mip count requests the complete chain");
    d = rgba(); d.format = 71; d.depth_or_array = 1; d.mip_levels = 1;
    check(Texture::make(d, texture) == Result::Ok && texture.layout().planes[0].mips[0].width_elements == 2 &&
              texture.layout().planes[0].mips[0].height_elements == 2 &&
              texture.layout().planes[0].element_bytes == 8, "BC1 edge dimensions use 4x4 blocks");
    d = rgba(); d.dimension = 2; d.height = 1;
    check(Texture::make(d, texture) == Result::Ok, "1D array layout");
    d = rgba(); d.dimension = 4; d.width = 4; d.height = 4; d.depth_or_array = 3; d.mip_levels = 1;
    check(Texture::make(d, texture) == Result::Ok && texture.layout().planes[0].mips[0].size_bytes == 3072,
          "volume footprint keeps three depth slices");
}

void failures_preserve_layout() {
    Texture texture;
    check(Texture::make(rgba(), texture) == Result::Ok, "baseline footprint builds");
    const auto baseline = texture.layout().planes[0].mips[0].size_bytes;
    auto reject = [&](Description d, Result wanted, const char* label) {
        check(Texture::make(d, texture) == wanted && texture.layout().planes[0].mips[0].size_bytes == baseline, label);
    };
    auto d = rgba(); d.width = UINT64_MAX; reject(d, Result::Invalid, "huge width fails without overflow or mutation");
    d = rgba(); d.mip_levels = 15; reject(d, Result::Invalid, "mips beyond complete chain fail");
    d = rgba(); d.height = 0; reject(d, Result::Invalid, "zero height fails");
    d = rgba(); d.layout = 0; reject(d, Result::Unsupported, "automatic layout is not silently made linear");
    d = rgba(); d.layout = 0x10a; reject(d, Result::Unsupported, "tiled layout is refused");
    d = rgba(); d.sample_count = 4; reject(d, Result::Unsupported, "multisampling is refused");
    d = rgba(); d.format = 20; reject(d, Result::Unsupported, "depth/stencil planes are refused");
    d = rgba(); d.format = 103; reject(d, Result::Unsupported, "video planar format is refused");
    d = rgba(); d.format = 66; reject(d, Result::Unsupported, "one-bit pixels are refused");
    d = rgba(); d.format = 999; reject(d, Result::Unsupported, "unknown format is not assumed RGBA8");
    d = rgba(); d.flags = 1; reject(d, Result::Unsupported, "unconfirmed flagged-resource layouts are refused");
}

void copies() {
    Texture texture; check(Texture::make(rgba(), texture) == Result::Ok, "copy layout builds");
    std::vector<std::byte> target(65536, std::byte{0xcc});
    std::vector<std::byte> source(24);
    for (size_t i = 0; i < source.size(); ++i) source[i] = std::byte(i + 1);
    check(texture.copy_into(0, 4, source, 12, 0, target) == Result::Ok, "copy into array 1 mip 1");
    check(std::equal(source.begin(), source.begin() + 12, target.begin() + 3584) &&
              std::equal(source.begin() + 12, source.end(), target.begin() + 3584 + 256),
          "copy selects correct mip, array and rows");
    check(target[3583] == std::byte{0xcc} && target[3584 + 12] == std::byte{0xcc} &&
              target[3584 + 256 + 12] == std::byte{0xcc}, "padding remains untouched");
    const auto before = target;
    check(texture.copy_into(0, 4, source, 11, 0, target) == Result::Invalid && target == before,
          "short row pitch rejects before writing");
    check(texture.copy_into(0, 4, std::span(source).first(23), 12, 0, target) == Result::Invalid && target == before,
          "short source rejects before writing");
    check(texture.copy_into(0, 4, source, 12, 0, std::span(target).first(3600)) == Result::Invalid && target == before,
          "short destination rejects before writing");
    check(texture.copy_into(1, 4, source, 12, 0, target) == Result::Invalid && target == before,
          "unknown plane rejects before writing");
    check(texture.copy_into(0, 6, source, 12, 0, target) == Result::Invalid && target == before,
          "out-of-range subresource rejects before writing");
    check(texture.copy_into(0, 4, std::span<const std::byte>(target).subspan(3584, 24), 12, 0, target) == Result::Invalid &&
              target == before, "partially overlapping source is refused before writing");
    auto d = rgba(); d.dimension = 4; d.width = 4; d.height = 4; d.depth_or_array = 3; d.mip_levels = 1;
    check(Texture::make(d, texture) == Result::Ok, "volume copy layout builds");
    source.assign(236, std::byte{0x5a}); target.assign(65536, std::byte{0xcc});
    check(texture.copy_into(0, 0, source, 20, 75, target) == Result::Invalid && target[0] == std::byte{0xcc},
          "volume slice pitch cannot overlap rows");
    check(texture.copy_into(0, 0, source, 20, 80, target) == Result::Ok && target[0] == std::byte{0x5a} &&
              target[1024] == std::byte{0x5a} && target[2048 + 3 * 256 + 15] == std::byte{0x5a},
          "volume copy addresses every depth slice");
    check(target[16] == std::byte{0xcc} && target[2048 + 3 * 256 + 16] == std::byte{0xcc},
          "volume copy retains row padding");
}
}

int main() {
    layouts(); failures_preserve_layout(); copies();
    std::printf("xg_xs_linear: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
