#include "../src/gfx10_compact.h"
#include <cstdio>
#include <vector>

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #c);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    using namespace leonida::gfx10;
    // FNV-1a of every output byte from tools/ttpf_mips.py::detile over synthetic
    // address-labelled input.
    constexpr std::uint64_t golden[2][8] = {
        {0x29dd008aeb5a8528, 0x4c8105b66d5f0a98, 0x5ebfde6a167d8d45, 0xc461f1e8d31e23a1, 0x97d3dfeb91200e25,
         0x8eff93dfb8141245, 0x02c46705bbb7041d, 0x484f4a88b8dc5491},
        {0x372440ab278fd715, 0x40dc1f13fc9ac5a5, 0x5ed106575d2c46ad, 0x68b6998ce92cde81, 0xcc4f627a848ab825,
         0xab561d0f8139b5c5, 0x4e55d750575b38ad, 0x6f5d4e55836588b5}};
    unsigned mode_index = 0;
    for (auto mode : {TileMode::sw_4kb_s, TileMode::sw_64kb_d}) {
        CompactSurface2D layout;
        CHECK(CompactSurface2D::make(mode, 128, 64, 8, 4, 1, layout));
        CHECK(layout.tiled_size() == (mode_index ? 131072 : 49152));
        CHECK(layout.first_tail() == (mode_index ? 1 : 3));
        std::vector<std::byte> source(layout.tiled_size());
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = std::byte((i * 37 + i / 251) & 255);
        for (unsigned level = 0; level < 8; ++level) {
            const auto *mip = layout.mip(level);
            CHECK(mip);
            const auto row = mip->width * 4, pitch = row + 13;
            std::vector<std::byte> output(pitch * mip->height, std::byte{0xab});
            CHECK(layout.detile(level, source, output, pitch));
            std::uint64_t hash = 14695981039346656037ull;
            for (std::size_t y = 0; y < mip->height; ++y) {
                for (std::size_t x = 0; x < row; ++x)
                    hash = (hash ^ std::to_integer<unsigned>(output[y * pitch + x])) * 1099511628211ull;
                for (std::size_t x = row; x < pitch; ++x)
                    CHECK(output[y * pitch + x] == std::byte{0xab});
            }
            CHECK(hash == golden[mode_index][level]);
            const auto before = output;
            CHECK(!layout.detile(level, std::span(source).first(source.size() - 1), output, pitch));
            CHECK(!layout.detile(level, source, output, row - 1));
            CHECK(!layout.detile(level, source, std::span(output).first(row - 1), pitch));
            CHECK(output == before);
        }
        CHECK(!layout.mip(8));
        CHECK(!layout.detile(8, source, source, 1));
        CHECK(!layout.detile(0, source, source, 512));
        const auto size = layout.tiled_size();
        CHECK(!CompactSurface2D::make(mode, 0, 64, 8, 4, 1, layout));
        CHECK(!CompactSurface2D::make(mode, 128, 64, 9, 4, 1, layout));
        CHECK(!CompactSurface2D::make(mode, 128, 64, 8, 3, 1, layout));
        CHECK(!CompactSurface2D::make(mode, 128, 64, 8, 4, 2, layout));
        CHECK(layout.tiled_size() == size);
        ++mode_index;
    }
    // Python reference hashes for odd pixel dimensions, both grids and every element width.
    constexpr std::uint64_t odd_hashes[4][5] = {{0xfbac73f5b6027c70ull, 0xbf191a0ce3a1788full, 0xea43500c053a7310ull,
                                                 0x85bf51377b1719ccull, 0x5b893f96b2da51a0ull},
                                                {0x2a3f1392014cd46cull, 0xabc52f0c8964d405ull, 0xb17a9b2f708b207dull,
                                                 0xcafdf634ba651235ull, 0x1ec2740d90009a50ull},
                                                {0x9b5f6366a561c2a5ull, 0x52b4c265e16c4fa2ull, 0x3c5146ddb81244fcull,
                                                 0x97af3826a39874edull, 0xa3a8215b4858ca5dull},
                                                {0xe2e4a2529db3c068ull, 0xb27e29195f9b6855ull, 0xc1efcdc866beaee4ull,
                                                 0xe1e9121d1c99a574ull, 0x64d9f2eb9b5d4844ull}};
    unsigned group = 0;
    for (auto mode : {TileMode::sw_4kb_s, TileMode::sw_64kb_d})
        for (unsigned block : {1, 4}) {
            unsigned column = 0;
            for (std::size_t bytes : {1, 2, 4, 8, 16}) {
                CompactSurface2D layout;
                CHECK(CompactSurface2D::make(mode, 131, 67, 8, bytes, block, layout));
                std::vector<std::byte> source(layout.tiled_size());
                for (std::size_t i = 0; i < source.size(); ++i)
                    source[i] = std::byte((i * 37 + i / 251) & 255);
                std::uint64_t hash = 14695981039346656037ull;
                for (unsigned level = 0; level < 8; ++level) {
                    const auto *mip = layout.mip(level);
                    std::vector<std::byte> output(mip->width * mip->height * bytes);
                    CHECK(layout.detile(level, source, output, mip->width * bytes));
                    for (auto b : output)
                        hash = (hash ^ std::to_integer<unsigned>(b)) * 1099511628211ull;
                }
                CHECK(hash == odd_hashes[group][column++]);
            }
            ++group;
        }
    CompactSurface2D empty;
    CHECK(!CompactSurface2D::make(TileMode::sw_64kb_d, UINT32_MAX, UINT32_MAX, 32, 16, 1, empty));
    CHECK(!empty.detile(0, {}, {}, 0));
    CHECK(CompactSurface2D::make(TileMode::sw_4kb_s, 1, 1, 1, 8, 4, empty));
    CHECK(empty.first_tail() == 1 && empty.tiled_size() == 4096);
    std::puts("gfx10_compact: 16 Python mip hashes, padding and invalid spans passed");
    return 0;
}
