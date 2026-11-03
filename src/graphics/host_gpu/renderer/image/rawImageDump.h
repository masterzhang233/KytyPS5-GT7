#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>

namespace Libs::Graphics::ImageDump::Raw {

// Describes the actual readback, not an inferred semantic such as motion vectors.
// All columns of each selected row are copied. Row n came from source y=n*row_step.
struct Layout {
    uint64_t guest_address = 0, guest_bytes = 0, content_serial = 0;
    uint32_t capture = 0, operation = 0, vk_format = 0, guest_format = 0, guest_tile_mode = 0;
    uint32_t guest_width = 0, guest_height = 0, guest_depth = 0;
    uint32_t host_width = 0, host_height = 0, host_depth = 0, host_layers = 0, host_mips = 0;
    uint32_t mip = 0, width = 0, height = 0, rows = 0, row_step = 1;
    uint32_t tiles = 0, available_tiles = 0, texel_bytes = 0;
    uint64_t row_pitch = 0, tile_pitch = 0;
    bool depth = false, volume = false;
};

struct Result {
    bool written = false;
    uint64_t payload_bytes = 0;
    std::string error;
};

// Rejects invalid/truncated input, arithmetic overflow and overlapping rows/tiles.
// A valid layout may intentionally cover only selected rows/tiles of one mip.
bool Validate(const Layout& layout, uint64_t source_bytes, uint64_t& payload_bytes,
              std::string& error);

// Synchronous, serialized writer. Budget is process-wide when the same Writer is reused;
// it includes binary and JSON bytes and is retained conservatively after an I/O failure.
// The JSON is published last and only then has complete=true. Existing files are never replaced.
class Writer {
public:
    explicit Writer(uint64_t budget_bytes): m_budget(budget_bytes) {}
    Result Write(const std::filesystem::path& directory, const std::string& name,
                 const std::string& description, const Layout& layout,
                 std::span<const uint8_t> bytes);
private:
    uint64_t m_budget = 0, m_used = 0;
    std::mutex m_mutex;
};

} // namespace Libs::Graphics::ImageDump::Raw
