#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

// A deliberately bounded host-linear layout, not an implementation of Xbox tiled layout computation.
// Descriptor offsets and the three consumed mip fields are measured; docs/local_xg_linear.md separates them
// from the inherited layout assumptions. Adapted from the XG portion of upstream PR #8134.
namespace rt::xg_linear {

constexpr uint32_t kExplicitLinear = 0x100;
constexpr size_t kMaxMips = 15;
constexpr uint64_t kRowAlignment = 256;
constexpr uint64_t kSubresourceAlignment = 512;
constexpr uint64_t kResourceAlignment = 65536;

enum class Result { Ok, Invalid, Unsupported };

struct Description {
    uint32_t dimension;
    uint64_t alignment;
    uint64_t width;
    uint32_t height;
    uint16_t depth_or_array;
    uint16_t mip_levels;
    uint32_t format;
    uint32_t sample_count;
    uint32_t sample_quality;
    uint32_t layout;
    uint32_t flags;
};

struct FormatShape { uint32_t bytes, block; };

inline FormatShape format_shape(uint32_t format) {
    if (format >= 1 && format <= 4) return {16, 1};
    if (format >= 5 && format <= 8) return {12, 1};
    if (format >= 9 && format <= 18) return {8, 1};
    if (format >= 23 && format <= 43 && format != 40) return {4, 1};
    if (format >= 48 && format <= 59 && format != 55) return {2, 1};
    if (format >= 60 && format <= 65) return {1, 1};
    if (format == 67 || (format >= 87 && format <= 93)) return {4, 1};
    if ((format >= 70 && format <= 72) || (format >= 79 && format <= 81)) return {8, 4};
    if ((format >= 73 && format <= 78) || (format >= 82 && format <= 84) ||
        (format >= 94 && format <= 99)) return {16, 4};
    if (format == 85 || format == 86 || format == 115) return {2, 1};
    return {};  // Depth/stencil, planar/video, one-bit and unknown formats need separate layout rules.
}

struct MipLayout {
    uint64_t size_bytes;
    uint64_t offset_bytes;
    uint64_t slice_bytes;
    uint32_t pitch_pixels;
    uint32_t row_pitch;
    uint32_t alignment_bytes;
    uint32_t padded_width;
    uint32_t padded_height;
    uint32_t padded_depth;
    uint32_t width_elements;
    uint32_t height_elements;
    uint32_t depth_or_array;
    uint32_t samples;
    uint32_t tile_mode;
    std::array<std::byte, 0x70 - 0x44> unknown;
};

struct PlaneLayout {
    uint32_t usage;
    uint32_t padding;
    uint64_t size_bytes;
    uint64_t base_offset;
    uint64_t alignment_bytes;
    uint32_t element_bytes;
    std::array<std::byte, 0x38 - 0x24> unknown;
    std::array<MipLayout, kMaxMips> mips;
};

struct ResourceLayout {
    uint64_t size_bytes;
    uint64_t alignment_bytes;
    uint32_t mip_levels;
    uint32_t plane_count;
    std::array<PlaneLayout, 4> planes;
};

static_assert(sizeof(MipLayout) == 0x70);
static_assert(offsetof(MipLayout, size_bytes) == 0 && offsetof(MipLayout, slice_bytes) == 0x10);
static_assert(offsetof(MipLayout, row_pitch) == 0x1c);
static_assert(sizeof(PlaneLayout) == 0x6c8 && offsetof(PlaneLayout, mips) == 0x38);
static_assert(offsetof(ResourceLayout, planes) == 0x18 && sizeof(ResourceLayout) == 0x1b38);

inline bool align_up(uint64_t value, uint64_t alignment, uint64_t& out) {
    if (value > (std::numeric_limits<uint64_t>::max)() - (alignment - 1)) return false;
    out = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

inline Result validate(const Description& d) {
    if (d.dimension < 2 || d.dimension > 4 || !d.width || !d.height || !d.depth_or_array ||
        !d.sample_count) return Result::Invalid;
    if (d.dimension == 2 && d.height != 1) return Result::Invalid;
    const uint64_t limit = d.dimension == 4 ? 2048 : 16384;
    if (d.width > limit || d.height > limit || d.depth_or_array > 2048) return Result::Invalid;
    if (d.layout != kExplicitLinear || d.sample_count != 1 || d.sample_quality ||
        (d.alignment != 0 && d.alignment != kResourceAlignment) || !format_shape(d.format).bytes ||
        d.flags != 0) return Result::Unsupported;
    return Result::Ok;
}

class Texture {
public:
    static Result make(const Description& d, Texture& out) {
        const Result valid = validate(d);
        if (valid != Result::Ok) return valid;
        uint32_t full_mips = 1;
        uint64_t largest = std::max<uint64_t>({d.width, d.height, d.dimension == 4 ? d.depth_or_array : 1u});
        while ((largest >>= 1) != 0) ++full_mips;
        const uint32_t count = d.mip_levels ? d.mip_levels : full_mips;
        if (count > full_mips || count > kMaxMips) return Result::Invalid;
        Texture next;
        next.description_ = d;
        auto& layout = next.layout_;
        layout.mip_levels = count;
        layout.plane_count = 1;
        layout.alignment_bytes = kResourceAlignment;
        auto& plane = layout.planes[0];
        plane.alignment_bytes = kResourceAlignment;
        const FormatShape shape = format_shape(d.format);
        plane.element_bytes = shape.bytes;
        uint64_t offset = 0;
        for (uint32_t mip = 0; mip < count; ++mip) {
            const uint64_t width = std::max<uint64_t>(d.width >> mip, 1);
            const uint32_t height = std::max<uint32_t>(d.height >> mip, 1);
            const uint32_t depth = d.dimension == 4 ? std::max<uint32_t>(d.depth_or_array >> mip, 1)
                                                    : d.depth_or_array;
            const uint32_t elements_x = static_cast<uint32_t>((width + shape.block - 1) / shape.block);
            const uint32_t elements_y = (height + shape.block - 1) / shape.block;
            uint64_t pitch, slice;
            if (!align_up(uint64_t(elements_x) * shape.bytes, kRowAlignment, pitch) ||
                !align_up(pitch * elements_y, kSubresourceAlignment, slice)) return Result::Invalid;
            auto& record = plane.mips[mip];
            record.size_bytes = slice * depth;
            record.offset_bytes = offset;
            record.slice_bytes = slice;
            record.row_pitch = static_cast<uint32_t>(pitch);
            record.pitch_pixels = static_cast<uint32_t>(pitch / shape.bytes * shape.block);
            record.alignment_bytes = kSubresourceAlignment;
            record.padded_width = static_cast<uint32_t>(pitch / shape.bytes);
            record.padded_height = elements_y;
            record.padded_depth = depth;
            record.width_elements = elements_x;
            record.height_elements = elements_y;
            record.depth_or_array = depth;
            record.samples = 1;
            if (offset > (std::numeric_limits<uint64_t>::max)() - record.size_bytes) return Result::Invalid;
            offset += record.size_bytes;
        }
        if (!align_up(offset, kResourceAlignment, plane.size_bytes) ||
            plane.size_bytes > (std::numeric_limits<size_t>::max)()) return Result::Invalid;
        layout.size_bytes = plane.size_bytes;
        out = next;
        return Result::Ok;
    }

    const ResourceLayout& layout() const { return layout_; }

    struct CopyRange { uint64_t offset, row_bytes, rows, depth, source_size, destination_size; };

    Result copy_range(uint32_t plane, uint32_t subresource, uint32_t row_pitch, uint32_t slice_pitch,
                      CopyRange& out) const {
        if (!layout_.mip_levels || plane != 0) return Result::Invalid;
        const uint32_t mip = subresource % layout_.mip_levels;
        const uint32_t array = subresource / layout_.mip_levels;
        const bool volume = description_.dimension == 4;
        if (array >= (volume ? 1 : description_.depth_or_array)) return Result::Invalid;
        const auto& record = layout_.planes[0].mips[mip];
        const uint64_t bytes = uint64_t(record.width_elements) * layout_.planes[0].element_bytes;
        const uint64_t row_extent = uint64_t(row_pitch) * (record.height_elements - 1) + bytes;
        const uint64_t depth = volume ? record.depth_or_array : 1;
        if (row_pitch < bytes || (depth > 1 && slice_pitch < row_extent)) return Result::Invalid;
        const uint64_t offset = record.offset_bytes + (volume ? 0 : uint64_t(array) * record.slice_bytes);
        const uint64_t source_size = (depth - 1) * slice_pitch + row_extent;
        const uint64_t destination_size = offset + (depth - 1) * record.slice_bytes +
                                          (record.height_elements - 1) * record.row_pitch + bytes;
        if (source_size > (std::numeric_limits<size_t>::max)() || destination_size > layout_.size_bytes)
            return Result::Invalid;
        out = {offset, bytes, record.height_elements, depth, source_size, destination_size};
        return Result::Ok;
    }

    Result copy_into(uint32_t plane, uint32_t subresource, std::span<const std::byte> source,
                     uint32_t row_pitch, uint32_t slice_pitch, std::span<std::byte> destination) const {
        CopyRange range{};
        if (copy_range(plane, subresource, row_pitch, slice_pitch, range) != Result::Ok ||
            source.size() < range.source_size || destination.size() < range.destination_size) return Result::Invalid;
        const auto a = reinterpret_cast<uintptr_t>(source.data());
        const auto b = reinterpret_cast<uintptr_t>(destination.data());
        if (!a || !b || range.source_size > UINTPTR_MAX - a || range.destination_size > UINTPTR_MAX - b ||
            (a <= b ? b - a < range.source_size : a - b < range.destination_size))
            return Result::Invalid;
        const auto& record = layout_.planes[0].mips[subresource % layout_.mip_levels];
        for (uint64_t z = 0; z < range.depth; ++z)
            for (uint64_t y = 0; y < range.rows; ++y)
                std::memcpy(destination.data() + range.offset + z * record.slice_bytes + y * record.row_pitch,
                            source.data() + z * slice_pitch + y * row_pitch, static_cast<size_t>(range.row_bytes));
        return Result::Ok;
    }

private:
    Description description_{};
    ResourceLayout layout_{};
};

}  // namespace rt::xg_linear
