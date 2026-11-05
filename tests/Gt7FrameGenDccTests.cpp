#include "graphics/host_gpu/renderer/gt7FrameGenDcc.h"

#include <cstdio>
#include <vector>

using namespace Libs::Graphics::Gt7FrameGenDcc;

int main() {
    int failures = 0;
    const auto check = [&](bool good, const char* name) {
        if (!good) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    const CopyContract captured {CopyShader,
        {0x00030000, 0x00010012, 0x00028000, 0x00005204},
        {0x06a30000, 0x00020002, 0x00014000, 0x0001222c},
        {8, 40, 1}, {8, 8, 1}, 0x41, 8, false};
    check(Matches(captured), "captured integer DCC swizzle contract");
    for (int field = 0; field < 13; ++field) {
        auto bad = captured;
        switch (field) {
        case 0: ++bad.shader; break;
        case 1: ++bad.groups[0]; break;
        case 2: --bad.groups[1]; break;
        case 3: ++bad.threads[1]; break;
        case 4: bad.mode = 0x61; break;
        case 5: bad.thread_dimensions = true; break;
        case 6: ++bad.pitch; break;
        case 7: bad.source[1] |= 0x80000000u; break;
        case 8: --bad.source[2]; break;
        case 9: bad.source[3] |= 0x20000000u; break;
        case 10: --bad.destination[2]; break;
        case 11: bad.destination[3] ^= 1; break;
        case 12: bad.destination[0] = bad.source[0];
                 bad.destination[1] = (bad.destination[1] & 0xffff0000u) | 0x12; break;
        }
        check(!Matches(bad), "unsupported shape/descriptor/overlap rejected");
    }
    std::vector<uint8_t> bytes(Bytes, 0xff);
    check(FullyExpanded(bytes), "entire expanded metadata accepted");
    // Scan the whole input: the former frame-graph preview covered only 1 KiB.
    for (const auto offset : {0u, 1024u, Bytes / 2, Bytes - 1}) {
        for (const auto value : {0x00, 0x20, 0x40, 0x80, 0xc0, 0xfe}) {
            bytes[offset] = static_cast<uint8_t>(value);
            check(!FullyExpanded(bytes), "one non-expanded byte anywhere rejected");
        }
        bytes[offset] = 0xff;
    }
    bytes.pop_back();
    check(!FullyExpanded(bytes), "truncated source rejected");
    std::printf("GT7 DCC contract checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
