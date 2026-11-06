#include "common/abi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Guest ABI structures used by these exported library functions.
namespace Libs::Font {
struct FontGlyphMetrics {
    float width;
    float height;
    struct { float bearing_x; float bearing_y; float advance; } horizontal;
    struct { float bearing_x; float bearing_y; float advance; } vertical;
};
struct FontKerning { float offset_x; float offset_y; float position_x; float position_y; };
struct FontRenderSurface {
    void* buffer;
    int32_t width_byte;
    int8_t pixel_size_byte;
    uint8_t system_ext0;
    uint8_t system_ext1;
    uint8_t system_ext2;
    int32_t width;
    int32_t height;
    struct { uint32_t x0; uint32_t y0; uint32_t x1; uint32_t y1; } scissor;
    uint32_t system_use[22];
};
struct FontRenderResult;
int KYTY_SYSV_ABI FontOpenFontMemory(void*, const void*, uint32_t, const void*, void**);
int KYTY_SYSV_ABI FontCloseFont(void*);
int KYTY_SYSV_ABI FontSetScalePixel(void*, float, float);
int KYTY_SYSV_ABI FontGetCharGlyphMetrics(void*, uint32_t, FontGlyphMetrics*);
int KYTY_SYSV_ABI FontGetKerning(void*, uint32_t, uint32_t, FontKerning*);
void KYTY_SYSV_ABI FontRenderSurfaceInit(FontRenderSurface*, void*, int, int, int, int);
int KYTY_SYSV_ABI FontRenderCharGlyphImageHorizontal(void*, uint32_t, FontRenderSurface*, float,
                                                   float, FontGlyphMetrics*, FontRenderResult*);
} // namespace Libs::Font

namespace {
void CheckFont(bool result, const char* message) {
    if (!result) {
        std::fprintf(stderr, "FontUnicodeTests: %s\n", message);
        std::abort();
    }
}

std::vector<uint8_t> ReadFont(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Font {
    void* handle = nullptr;
    explicit Font(const std::vector<uint8_t>& data) {
        CheckFont(Libs::Font::FontOpenFontMemory(nullptr, data.data(),
                  static_cast<uint32_t>(data.size()), nullptr, &handle) == 0 && handle,
                  "could not open font memory");
        CheckFont(Libs::Font::FontSetScalePixel(handle, 32.0f, 32.0f) == 0, "could not set scale");
    }
    ~Font() { Libs::Font::FontCloseFont(handle); }
    Font(const Font&) = delete;
};

struct Glyph {
    std::array<uint8_t, 64 * 64> pixels {};
    Libs::Font::FontGlyphMetrics metrics {};
};

Glyph Render(void* font, uint32_t code) {
    using namespace Libs::Font;
    Glyph glyph;
    FontRenderSurface surface {};
    FontRenderSurfaceInit(&surface, glyph.pixels.data(), 64, 1, 64, 64);
    CheckFont(FontRenderCharGlyphImageHorizontal(font, code, &surface, 8.0f, 44.0f,
              &glyph.metrics, nullptr) == 0, "could not render character");
    FontGlyphMetrics query {};
    CheckFont(FontGetCharGlyphMetrics(font, code, &query) == 0 &&
              std::memcmp(&query, &glyph.metrics, sizeof(query)) == 0,
              "measured and rendered glyphs use different metrics");
    return glyph;
}

std::filesystem::path InstalledCjkFont() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> windows {};
    const auto size = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (size == 0 || size >= windows.size()) return {};
    for (const char* name : {"msyh.ttc", "simhei.ttf", "simsun.ttc", "msgothic.ttc"}) {
        const auto path = std::filesystem::path(windows.data()) / "Fonts" / name;
        if (std::filesystem::is_regular_file(path)) return path;
    }
#endif
    return {};
}
} // namespace

void TestFontUnicodeFallback() {
    using namespace Libs::Font;
    const auto latin = ReadFont(KYTY_TEST_LATIN_FONT);
    CheckFont(!latin.empty(), "missing open-licensed Latin test fixture");
    Font opened(latin);
    const auto question = Render(opened.handle, '?');
    const auto latin_before = Render(opened.handle, 'A');
    CheckFont(std::ranges::any_of(latin_before.pixels, [](uint8_t x) { return x != 0; }) &&
              latin_before.pixels != question.pixels, "original Latin glyph did not render");

    const auto bundled_data = ReadFont(KYTY_TEST_CJK_FONT);
    CheckFont(!bundled_data.empty(), "missing pinned open-licensed CJK test fixture");
    Font bundled(bundled_data);
    {
        // Separate handles first request the same missing CJK glyph concurrently. The cached
        // font storage must remain stable and load only once for metrics and rasterization.
        std::array<std::thread, 4> threads;
        for (auto& thread : threads) thread = std::thread([&] {
            Font own(latin);
            const auto glyph = Render(own.handle, 0x4e2d); // Chinese: 中
            CheckFont(std::ranges::any_of(glyph.pixels, [](uint8_t x) { return x != 0; }) &&
                      glyph.pixels != question.pixels, "missing Chinese became a question mark");
        });
        for (auto& thread : threads) thread.join();
        for (const auto code : {0x4e2du, 0x6587u, 0x8f66u, 0x9053u, 0x3042u,
                                0x30a2u, 0xac00u, 0xd55cu, 0x3002u}) {
            const auto glyph = Render(opened.handle, code);
            const auto direct = Render(bundled.handle, code);
            CheckFont(std::ranges::any_of(glyph.pixels, [](uint8_t x) { return x != 0; }) &&
                      glyph.pixels != question.pixels, "CJK glyph fallback missing");
            CheckFont(glyph.metrics.horizontal.advance > 0.0f, "CJK advance missing");
            CheckFont(glyph.pixels == direct.pixels &&
                      std::memcmp(&glyph.metrics, &direct.metrics, sizeof(glyph.metrics)) == 0,
                      "fallback did not use the bundled CJK face for its bitmap and metrics");
        }
        FontKerning kerning {};
        CheckFont(FontGetKerning(opened.handle, 'A', 0x4e2d, &kerning) == 0 &&
                  kerning.offset_x == 0.0f, "cross-font kerning must be zero");
        CheckFont(FontGetKerning(opened.handle, 0x4e2d, 0x6587, &kerning) == 0,
                  "same fallback face kerning failed");
        std::puts("FontUnicodeTests: bundled Noto CJK fallback, CJK glyphs and metrics passed");
    }

    const auto cjk_path = InstalledCjkFont();
    if (!cjk_path.empty()) {
        // Open the installed collection itself through the guest API. Its font directory
        // and TrueType tables have collection-relative offsets, not an sfnt at byte zero.
        const auto cjk_data = ReadFont(cjk_path);
        Font collection(cjk_data);
        const auto direct = Render(collection.handle, 0x4e2d);
        CheckFont(direct.pixels != Render(collection.handle, '?').pixels,
                  "TTC/TTF guest memory face did not render Chinese");
        std::printf("FontUnicodeTests: installed CJK collection checks passed (%s)\n",
                    cjk_path.filename().string().c_str());
    } else {
        std::puts("FontUnicodeTests: no installed CJK test font; optional collection checks skipped");
    }
    CheckFont(Render(opened.handle, 'A').pixels == latin_before.pixels,
              "fallback replaced the game's original Latin face");
    CheckFont(Render(opened.handle, 0x10ffffu).pixels == question.pixels &&
              Render(opened.handle, 0xffffffffu).pixels == question.pixels &&
              Render(opened.handle, 0xd800u).pixels == question.pixels,
              "unsupported/invalid code point did not retain question-mark fallback");
    FontKerning missing {};
    CheckFont(FontGetKerning(opened.handle, 'A', 0x10ffffu, &missing) != 0,
              "unsupported code point kerning should report no support");

    // Truncated and out-of-range collection headers are refused before reaching stb.
    const std::vector<uint8_t> truncated {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 1};
    const std::vector<uint8_t> invalid {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 1,
                                       0xff, 0xff, 0xff, 0xff};
    Font bad1(truncated), bad2(invalid);
    CheckFont(std::ranges::any_of(Render(bad1.handle, 'A').pixels, [](uint8_t x) { return x != 0; }) &&
              std::ranges::any_of(Render(bad2.handle, 'A').pixels, [](uint8_t x) { return x != 0; }),
              "invalid collection failed to select a safe fallback");
    std::puts("FontUnicodeTests: original glyphs, fallback metrics/bitmap, invalid input checks passed");
}
