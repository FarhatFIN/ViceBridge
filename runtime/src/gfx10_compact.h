#pragma once

#include "gfx10_tiling.h"
#include <algorithm>
#include <array>

namespace leonida::gfx10 {

// Container-independent port of tools/tex_headers.py::compact_surface_layout.
// One 2D allocation, no arrays, tile XOR or format inference. Dimensions are
// pixels; element_bytes describes a pixel (block=1) or a compressed 4x4 block
// (block=4).
class CompactSurface2D {
  public:
    struct Mip {
        std::size_t width = 0, height = 0, offset = 0, region = 0, origin_x = 0, origin_y = 0;
        TiledSurface2D surface;
    };

    static bool make(TileMode mode, std::uint32_t width, std::uint32_t height, unsigned mips, std::size_t element_bytes,
                     unsigned block, CompactSurface2D &out) {
        if (!width || !height || !mips || mips > static_cast<unsigned>(std::bit_width(std::max(width, height))) ||
            (block != 1 && block != 4))
            return false;
        TiledSurface2D probe;
        if (!TiledSurface2D::make(mode, element_bytes, 1, 1, probe))
            return false;
        CompactSurface2D next;
        next.count_ = mips;
        next.element_bytes_ = element_bytes;
        next.first_tail_ = mips;
        const auto bw = probe.tile_width(), bh = probe.tile_height(), tile = probe.tiled_size();
        const unsigned max_tail = mode == TileMode::sw_4kb_s ? 8 : 12;
        const auto log = std::countr_zero(element_bytes);
        const std::size_t origin_w = std::size_t{16} >> (log / 2);
        const std::size_t origin_h = std::size_t{16} >> ((log + 1) / 2);
        const std::size_t ew = (std::size_t(width) - 1) / block + 1;
        const std::size_t eh = (std::size_t(height) - 1) / block + 1;
        for (unsigned i = 0; i < mips; ++i) {
            auto &mip = next.mips_[i];
            mip.width = ((ew - 1) >> i) + 1;
            mip.height = ((eh - 1) >> i) + 1;
            if (next.first_tail_ == mips && mips > 1 && mip.width <= bw / 2 && mip.height <= bh && mips - i <= max_tail)
                next.first_tail_ = i;
            if (i < next.first_tail_) {
                if (!TiledSurface2D::make(mode, element_bytes, mip.width, mip.height, mip.surface))
                    return false;
                mip.region = mip.surface.tiled_size();
            } else {
                const unsigned entry = max_tail - 1 - (i - next.first_tail_);
                const std::size_t moff = entry > 6 ? std::size_t{16} << entry : entry << 8;
                std::size_t x = 0, y = 0;
                for (unsigned bit = 0; bit < 6; ++bit) {
                    x |= ((moff >> (9 + 2 * bit)) & 1) << bit;
                    y |= ((moff >> (8 + 2 * bit)) & 1) << bit;
                }
                mip.origin_x = x * origin_w;
                mip.origin_y = y * origin_h;
                if (mip.origin_x > bw || mip.width > bw - mip.origin_x || mip.origin_y > bh ||
                    mip.height > bh - mip.origin_y || !TiledSurface2D::make(mode, element_bytes, bw, bh, mip.surface))
                    return false;
                mip.region = tile;
            }
        }
        next.size_ = next.first_tail_ < mips ? tile : 0;
        for (unsigned i = next.first_tail_; i > 0; --i) {
            auto &mip = next.mips_[i - 1];
            mip.offset = next.size_;
            if (mip.region > std::numeric_limits<std::size_t>::max() - next.size_)
                return false;
            next.size_ += mip.region;
        }
        out = next;
        return true;
    }

    std::size_t tiled_size() const { return size_; }
    unsigned first_tail() const { return first_tail_; }
    const Mip *mip(unsigned level) const { return level < count_ ? &mips_[level] : nullptr; }

    // Requires the complete allocation; leaves destination and padding unchanged
    // on invalid input.
    bool detile(unsigned level, std::span<const std::byte> source, std::span<std::byte> destination,
                std::size_t pitch) const {
        const auto *m = mip(level);
        if (!m || source.size() < size_ || tiling_detail::overlap(source, destination))
            return false;
        const auto row_bytes = m->width * element_bytes_;
        if (pitch < row_bytes ||
            (m->height > 1 && pitch > (std::numeric_limits<std::size_t>::max() - row_bytes) / (m->height - 1)) ||
            destination.size() < (m->height - 1) * pitch + row_bytes)
            return false;
        for (std::size_t y = 0; y < m->height; ++y)
            for (std::size_t x = 0; x < m->width; ++x) {
                std::size_t offset = 0;
                // make() bounds every mip rectangle inside its immutable addressing surface.
                m->surface.element_offset(x + m->origin_x, y + m->origin_y, offset);
                std::memcpy(destination.data() + y * pitch + x * element_bytes_, source.data() + m->offset + offset,
                            element_bytes_);
            }
        return true;
    }

  private:
    std::array<Mip, 32> mips_{};
    unsigned count_ = 0, first_tail_ = 0;
    std::size_t size_ = 0, element_bytes_ = 0;
};

} // namespace leonida::gfx10
