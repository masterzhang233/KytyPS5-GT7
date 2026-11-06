#include "common/abi.h"

#include <algorithm>
#include <array>
#include <cmath>
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
struct FontMemory;
struct FontCreateStringDetail;
struct FontTextSource;
union FontTextParseResult;
using FontTextParseFunction = KYTY_SYSV_ABI int (*)(FontTextSource*, void**, FontTextParseResult*);
struct FontTextSource {
    uint64_t system_use0;
    const void* start;
    const void* end;
    const void* current;
    FontTextParseFunction text_parser;
    void* text_object;
    void* default_font;
    void* system_use[5];
};
struct FontWriting { void* system_use[32]; };
struct FontWritingExtent { float top; float bottom; float left; float right; };
struct FontWritingMetrics {
    float advance_x;
    float advance_y;
    FontWritingExtent extent;
};
struct FontWritingStep {
    float x;
    float y;
    float advance_x;
    float advance_y;
    void* font;
    struct { uint32_t character_count : 8; uint32_t invisible_glyph : 1; } profile;
    uint32_t glyph_code;
    struct { float x; float y; } positioning;
    FontGlyphMetrics glyph_metrics;
};
struct FontWritingLetterStep {
    float x;
    float y;
    float advance_x;
    float advance_y;
    struct {
        uint32_t texts_count;
        uint32_t texts_index;
        uint32_t glyphs_count;
        uint32_t glyphs_index;
        uint8_t base_components_count;
        uint8_t base_components_index;
        uint8_t base_focus_count;
        uint8_t base_focus_index;
        uint8_t opposite_direction;
        uint8_t character_text_count;
        uint8_t base_text_count;
        uint8_t mark_text_count;
        uint32_t marks_text_count;
        uint32_t marks_text_number;
        uint32_t reserved[3];
        int32_t text_letter_offset;
    } components;
};
struct FontWritingLineStep {
    float x;
    float y;
    float advance_x;
    float advance_y;
    float spacing_progress;
    void* writing_orderer;
    struct { float x; float y; } adjusting;
    FontWritingMetrics metrics;
};
int KYTY_SYSV_ABI FontOpenFontMemory(void*, const void*, uint32_t, const void*, void**);
int KYTY_SYSV_ABI FontCloseFont(void*);
int KYTY_SYSV_ABI FontSetScalePixel(void*, float, float);
int KYTY_SYSV_ABI FontGetCharGlyphMetrics(void*, uint32_t, FontGlyphMetrics*);
int KYTY_SYSV_ABI FontGetKerning(void*, uint32_t, uint32_t, FontKerning*);
void KYTY_SYSV_ABI FontRenderSurfaceInit(FontRenderSurface*, void*, int, int, int, int);
int KYTY_SYSV_ABI FontRenderCharGlyphImageHorizontal(void*, uint32_t, FontRenderSurface*, float,
                                                   float, FontGlyphMetrics*, FontRenderResult*);
int KYTY_SYSV_ABI FontTextSourceInit(FontTextSource*, const void*, uint32_t, FontTextParseFunction, void*);
int KYTY_SYSV_ABI FontTextSourceSetDefaultFont(FontTextSource*, void*);
int KYTY_SYSV_ABI FontCreateString(const FontMemory*, FontTextSource*, const FontCreateStringDetail*, void**);
int KYTY_SYSV_ABI FontDestroyString(void**);
const void* KYTY_SYSV_ABI FontStringRefersTextCharacters(void*, uint32_t*);
const void* KYTY_SYSV_ABI FontStringRefersRenderCharacters(void*, const void*, const void*, uint32_t*);
const void* KYTY_SYSV_ABI FontCharacterRefersTextNext(const void*);
int KYTY_SYSV_ABI FontWritingInit(FontWriting*, void*, const void*);
const FontWritingStep* KYTY_SYSV_ABI FontWritingRefersRenderStep(FontWriting*);
const void* KYTY_SYSV_ABI FontWritingRefersRenderStepCharacter(FontWriting*, const void**);
int KYTY_SYSV_ABI FontWritingGetRenderMetrics(FontWriting*, FontWritingMetrics*);
int KYTY_SYSV_ABI FontCreateWritingLine(const FontMemory*, int, const void*, void**);
int KYTY_SYSV_ABI FontDestroyWritingLine(void**);
int KYTY_SYSV_ABI FontWritingLineWritesOrder(void*, uint64_t, const FontWritingMetrics*, void*);
FontWritingLineStep* KYTY_SYSV_ABI FontWritingLineRefersRenderStep(void*);
int KYTY_SYSV_ABI FontWritingLineGetRenderMetrics(void*, FontWritingMetrics*);
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

void CheckNear(float actual, float expected, const char* message) {
    CheckFont(std::abs(actual - expected) < 0.0001f, message);
}

struct TextString {
    void* handle = nullptr;
    TextString(void* font, const char* text) {
        using namespace Libs::Font;
        FontTextSource source {};
        CheckFont(FontTextSourceInit(&source, text, static_cast<uint32_t>(std::strlen(text)),
                                     nullptr, nullptr) == 0 &&
                  FontTextSourceSetDefaultFont(&source, font) == 0 &&
                  FontCreateString(nullptr, &source, nullptr, &handle) == 0 && handle,
                  "could not create writing string");
    }
    ~TextString() { Libs::Font::FontDestroyString(&handle); }
    TextString(const TextString&) = delete;
};

void TestFontWriting(void* latin, void* bundled) {
    using namespace Libs::Font;
    // Wide/narrow Latin followed by two CJK characters missing from the original face.
    TextString mixed(latin, "Wi\xe4\xb8\xad\xe6\x96\x87");
    TextString empty(latin, "");
    TextString other(latin, "X");
    uint32_t count = 99;
    std::array<const void*, 4> characters {};
    characters[0] = FontStringRefersTextCharacters(mixed.handle, &count);
    CheckFont(characters[0] && count == characters.size(), "UTF-8 writing character count incorrect");
    for (size_t i = 1; i < characters.size(); ++i)
        characters[i] = FontCharacterRefersTextNext(characters[i - 1]);
    CheckFont(characters.back() && !FontCharacterRefersTextNext(characters.back()),
              "text character iteration does not terminate");
    CheckFont(FontStringRefersRenderCharacters(mixed.handle, nullptr, characters.back(), &count) ==
                  characters.front() && count == 4, "inclusive range dropped the final character");
    CheckFont(FontStringRefersRenderCharacters(mixed.handle, characters.back(), characters.back(), &count) ==
                  characters.back() && count == 1, "single-character range is empty");
    CheckFont(FontStringRefersRenderCharacters(mixed.handle, characters[1], nullptr, &count) ==
                  characters[1] && count == 3, "open-ended range count incorrect");
    CheckFont(!FontStringRefersRenderCharacters(mixed.handle, characters.back(), characters.front(), &count) &&
                  count == 0, "reversed render range must be empty");
    const auto* foreign = FontStringRefersTextCharacters(other.handle, nullptr);
    CheckFont(!FontStringRefersRenderCharacters(mixed.handle, characters.front(), foreign, &count) &&
                  count == 0, "unreachable final character must be rejected");
    CheckFont(!FontStringRefersRenderCharacters(mixed.handle, characters.front(), foreign, nullptr),
              "invalid range accepted when the count output is omitted");
    CheckFont(!FontStringRefersRenderCharacters(empty.handle, nullptr, nullptr, &count) && count == 0 &&
              !FontStringRefersRenderCharacters(nullptr, nullptr, nullptr, &count) && count == 0,
              "empty or null string must have no render characters");

    FontWriting writing {};
    FontWritingMetrics metrics {};
    CheckFont(FontWritingInit(&writing, mixed.handle, nullptr) == 0 &&
              FontWritingGetRenderMetrics(&writing, &metrics) == 0 && metrics.advance_x == 0 &&
              metrics.extent.left == 0 && metrics.extent.right == 0 &&
              metrics.extent.top == 0 && metrics.extent.bottom == 0,
              "writing metrics must be empty before the first step");
    const void* letter_output = &writing;
    CheckFont(!FontWritingRefersRenderStepCharacter(&writing, &letter_output) && !letter_output,
              "character exposed before requesting a render step");
    constexpr std::array<uint32_t, 4> codes {'W', 'i', 0x4e2d, 0x6587};
    std::array<Glyph, 4> reference;
    for (size_t i = 0; i < codes.size(); ++i) reference[i] = Render(latin, codes[i]);
    CheckFont(reference[0].metrics.horizontal.advance > reference[1].metrics.horizontal.advance,
              "Latin fixture should distinguish wide and narrow glyph advances");
    CheckFont(reference[2].pixels == Render(bundled, codes[2]).pixels,
              "writing fixture did not select bundled CJK fallback");
    float pen = 0;
    FontWritingExtent expected_extent {};
    for (size_t i = 0; i < codes.size(); ++i) {
        const auto* step = FontWritingRefersRenderStep(&writing);
        CheckFont(step && step->glyph_code == codes[i] && step->font == latin &&
                  step->profile.character_count == 1, "writing step skipped or repeated a character");
        CheckNear(step->x, pen, "render step pen did not accumulate font advances");
        CheckNear(step->positioning.x, pen, "render positioning differs from pen position");
        CheckNear(step->advance_x, reference[i].metrics.horizontal.advance,
                  "writing advance differs from rendered font advance");
        CheckFont(std::memcmp(&step->glyph_metrics, &reference[i].metrics, sizeof(FontGlyphMetrics)) == 0,
                  "writing and rendering selected different glyph metrics/fallback faces");
        CheckFont(step->y == 0 && step->advance_y == 0 && step->positioning.y == 0,
                  "horizontal writing changed its vertical pen");
        CheckFont(FontWritingRefersRenderStepCharacter(&writing, &letter_output) == characters[i] && letter_output,
                  "step character does not match the requested step");
        const auto* letter = static_cast<const FontWritingLetterStep*>(letter_output);
        CheckNear(letter->x, pen, "letter step position differs from render step");
        CheckNear(letter->advance_x, step->advance_x, "letter step advance differs from render step");
        CheckFont(letter->components.texts_count == 1 && letter->components.texts_index == 0 &&
                  letter->components.glyphs_count == 1 && letter->components.glyphs_index == 0,
                  "letter component indices must be relative to the current step");
        CheckFont(!FontWritingRefersRenderStepCharacter(&writing, &letter_output) && !letter_output,
                  "step character was consumed more than once");
        const auto& glyph = reference[i].metrics;
        const FontWritingExtent bounds {-glyph.horizontal.bearing_y,
            -glyph.horizontal.bearing_y + glyph.height, pen + glyph.horizontal.bearing_x,
            pen + glyph.horizontal.bearing_x + glyph.width};
        if (i == 0) expected_extent = bounds;
        else {
            expected_extent.top = std::min(expected_extent.top, bounds.top);
            expected_extent.bottom = std::max(expected_extent.bottom, bounds.bottom);
            expected_extent.left = std::min(expected_extent.left, bounds.left);
            expected_extent.right = std::max(expected_extent.right, bounds.right);
        }
        pen += glyph.horizontal.advance;
        CheckFont(FontWritingGetRenderMetrics(&writing, &metrics) == 0, "writing metrics query failed");
        CheckNear(metrics.advance_x, pen, "accumulated writing advance is incorrect");
    }
    CheckNear(metrics.extent.left, expected_extent.left, "writing left extent incorrect");
    CheckNear(metrics.extent.right, expected_extent.right, "writing right extent incorrect");
    CheckNear(metrics.extent.top, expected_extent.top, "writing top extent incorrect");
    CheckNear(metrics.extent.bottom, expected_extent.bottom, "writing bottom extent incorrect");
    const auto complete = metrics;
    CheckFont(!FontWritingRefersRenderStep(&writing) && !FontWritingRefersRenderStep(&writing) &&
              !FontWritingRefersRenderStepCharacter(&writing, &letter_output) && !letter_output,
              "completed writing must stay exhausted");
    CheckFont(FontWritingGetRenderMetrics(&writing, &metrics) == 0 &&
              std::memcmp(&metrics, &complete, sizeof(metrics)) == 0,
              "exhausted iteration changed completed metrics");

    void* line = nullptr;
    CheckFont(FontCreateWritingLine(nullptr, 0x10, nullptr, &line) == 0 && line &&
              FontWritingLineWritesOrder(line, 0, &complete, mixed.handle) == 0 &&
              FontWritingLineWritesOrder(line, 0, &complete, other.handle) == 0 &&
              FontWritingLineGetRenderMetrics(line, &metrics) == 0, "writing-line ordering failed");
    CheckNear(metrics.advance_x, complete.advance_x * 2, "writing line lost actual font advances");
    const auto* line_step = FontWritingLineRefersRenderStep(line);
    CheckFont(line_step && line_step->x == 0 && line_step->writing_orderer == mixed.handle,
              "first writing-line step incorrect");
    line_step = FontWritingLineRefersRenderStep(line);
    CheckFont(line_step && line_step->writing_orderer == other.handle, "second writing-line step missing");
    CheckNear(line_step->x, complete.advance_x, "writing-line pen is not based on real font metrics");
    CheckFont(!FontWritingLineRefersRenderStep(line), "writing-line iteration does not terminate");
    CheckFont(FontDestroyWritingLine(&line) == 0 && !line, "writing-line cleanup failed");

    // Advancing without consuming a character must still progress, and reinitialization resets the pen.
    CheckFont(FontWritingInit(&writing, mixed.handle, characters[1]) == 0, "could not restart writing at a character");
    const auto* step = FontWritingRefersRenderStep(&writing);
    CheckFont(step && step->glyph_code == 'i' && step->x == 0, "restart did not reset the writing pen");
    step = FontWritingRefersRenderStep(&writing);
    CheckFont(step && step->glyph_code == 0x4e2d &&
              FontWritingRefersRenderStepCharacter(&writing, nullptr) == characters[2] &&
              !FontWritingRefersRenderStepCharacter(&writing, nullptr),
              "unconsumed previous character prevented progress or was returned twice");
    CheckFont(FontWritingInit(&writing, empty.handle, nullptr) == 0 &&
              !FontWritingRefersRenderStep(&writing) &&
              FontWritingGetRenderMetrics(&writing, &metrics) == 0 && metrics.advance_x == 0,
              "empty writing should have no step or advance");
    CheckFont(!FontWritingRefersRenderStep(nullptr) &&
              !FontWritingRefersRenderStepCharacter(nullptr, &letter_output) && !letter_output,
              "null writing must have no step/character");
    std::puts("FontUnicodeTests: mixed-face writing advances, inclusive ranges and step iteration passed");
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
    TestFontWriting(opened.handle, bundled.handle);

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
