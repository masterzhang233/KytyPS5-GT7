#include "graphics/host_gpu/renderer/image/rawImageDump.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Libs::Graphics::ImageDump::Raw {
namespace {
bool Multiply(uint64_t a, uint64_t b, uint64_t& out) {
    if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) return false;
    out = a * b;
    return true;
}
bool Add(uint64_t a, uint64_t b, uint64_t& out) {
    if (b > std::numeric_limits<uint64_t>::max() - a) return false;
    out = a + b;
    return true;
}
void Finish(std::ofstream& stream) {
    stream.flush();
    if (!stream) throw std::runtime_error("write/flush failed");
    stream.close();
    if (stream.fail()) throw std::runtime_error("close failed");
}
} // namespace

bool Validate(const Layout& l, uint64_t source_bytes, uint64_t& payload_bytes,
              std::string& error) {
    payload_bytes = 0;
    const auto fail = [&](const char* message) { error = message; return false; };
    if (!l.width || !l.height || !l.rows || !l.row_step || !l.tiles || !l.texel_bytes ||
        !l.host_mips || l.host_mips > 32 || l.mip >= l.host_mips ||
        !l.host_width || !l.host_height || !l.host_depth || !l.host_layers ||
        l.tiles > l.available_tiles) {
        return fail("invalid dimensions, mip or tile count");
    }
    const auto expected_width = std::max(l.host_width >> l.mip, 1u);
    const auto expected_height = std::max(l.host_height >> l.mip, 1u);
    const auto expected_tiles = l.volume ? std::max(l.host_depth >> l.mip, 1u) : l.host_layers;
    if (l.width != expected_width || l.height != expected_height ||
        l.available_tiles != expected_tiles) {
        return fail("selected mip dimensions do not match host image");
    }
    const auto expected_rows = (uint64_t{l.height} + l.row_step - 1) / l.row_step;
    if (l.rows != expected_rows) return fail("row count does not match sampled source rows");
    uint64_t row_bytes = 0, rows_span = 0, tile_offset = 0, required = 0, total = 0;
    if (!Multiply(l.width, l.texel_bytes, row_bytes) ||
        row_bytes > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
        l.row_pitch < row_bytes ||
        !Multiply(l.rows - 1, l.row_pitch, rows_span) ||
        !Add(rows_span, row_bytes, rows_span) || l.tile_pitch < rows_span ||
        !Multiply(l.tiles - 1, l.tile_pitch, tile_offset) ||
        !Add(tile_offset, rows_span, required) ||
        !Multiply(row_bytes, l.rows, total) || !Multiply(total, l.tiles, total)) {
        return fail("invalid strides or arithmetic overflow");
    }
    if (required > source_bytes) return fail("readback does not contain all described texels");
    payload_bytes = total;
    error.clear();
    return true;
}

Result Writer::Write(const std::filesystem::path& directory, const std::string& name,
                     const std::string& description, const Layout& l,
                     std::span<const uint8_t> bytes) {
    std::scoped_lock lock(m_mutex);
    Result result;
    if (!Validate(l, bytes.size(), result.payload_bytes, result.error)) return result;
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos) {
        result.error = "invalid output name";
        return result;
    }
    const auto bin = directory / (name + ".bin");
    const auto json = directory / (name + ".json");
    const auto bin_temp = directory / (name + ".bin.part");
    const auto json_temp = directory / (name + ".json.part");
    bool own_bin_temp = false, own_json_temp = false, own_bin = false;
    try {
        const uint64_t packed_row = uint64_t{l.width} * l.texel_bytes;
        nlohmann::json metadata = {
            {"schema", "kyty.raw-image.v1"}, {"complete", true},
            {"description", description}, {"semantic", "unclassified"},
            {"binary_file", bin.filename().string()}, {"payload_bytes", result.payload_bytes},
            {"byte_order", std::endian::native == std::endian::little ? "little" : "big"},
            {"value_conversion", "none; original texel bits, no normalization or gamma"},
            {"encoding", "Vulkan uncompressed texels of the selected aspect"},
            {"compressed_formats_supported", false},
            {"capture", l.capture}, {"operation", l.operation}, {"content_serial", l.content_serial},
            {"guest", {{"address", l.guest_address}, {"bytes", l.guest_bytes},
                       {"width", l.guest_width}, {"height", l.guest_height}, {"depth", l.guest_depth},
                       {"format", l.guest_format}, {"tile_mode", l.guest_tile_mode}}},
            {"host", {{"width", l.host_width}, {"height", l.host_height}, {"depth", l.host_depth},
                      {"layers", l.host_layers}, {"mips", l.host_mips}, {"vk_format", l.vk_format}}},
            {"selection", {{"mip", l.mip}, {"source_width", l.width}, {"source_height", l.height},
                           {"source_x_start", 0}, {"source_x_step", 1},
                           {"source_y_start", 0}, {"source_y_step", l.row_step},
                           {"rows", l.rows}, {"tile_start", 0}, {"tiles", l.tiles},
                           {"available_tiles", l.available_tiles},
                           {"tile_kind", l.volume ? "volume slices" : "array layers"},
                           {"rows_subsampled", l.row_step != 1},
                           {"tiles_truncated", l.tiles != l.available_tiles},
                           {"complete_selected_mip_aspect", l.row_step == 1 && l.tiles == l.available_tiles},
                           {"complete_texture", l.host_mips == 1 && l.mip == 0 &&
                                                  l.row_step == 1 && l.tiles == l.available_tiles && !l.depth}}},
            {"storage", {{"aspect", l.depth ? "depth" : "color"}, {"texel_bytes", l.texel_bytes},
                         {"width", l.width}, {"height", l.rows}, {"tiles", l.tiles},
                         {"row_pitch", packed_row}, {"tile_pitch", packed_row * l.rows},
                         {"padding_included", false}, {"order", "tile,row,column,texel byte"}}},
            {"readback", {{"bytes", bytes.size()}, {"row_pitch", l.row_pitch},
                          {"tile_pitch", l.tile_pitch}}}
        };
        const auto json_text = metadata.dump(2) + "\n";
        uint64_t charge = 0;
        if (!Add(result.payload_bytes, json_text.size(), charge) || m_used > m_budget ||
            charge > m_budget - m_used) {
            result.error = "process raw dump byte budget exceeded";
            return result;
        }
        std::filesystem::create_directories(directory);
        if (std::filesystem::exists(bin) || std::filesystem::exists(json) ||
            std::filesystem::exists(bin_temp) || std::filesystem::exists(json_temp)) {
            result.error = "output already exists";
            return result;
        }
        m_used += charge;
        std::ofstream raw(bin_temp, std::ios::binary | std::ios::trunc);
        own_bin_temp = true;
        if (!raw) throw std::runtime_error("cannot create binary temporary file");
        for (uint32_t tile = 0; tile < l.tiles; ++tile) {
            for (uint32_t row = 0; row < l.rows; ++row) {
                const auto offset = uint64_t{tile} * l.tile_pitch + uint64_t{row} * l.row_pitch;
                raw.write(reinterpret_cast<const char*>(bytes.data() + offset),
                          static_cast<std::streamsize>(packed_row));
                if (!raw) throw std::runtime_error("binary write failed");
            }
        }
        Finish(raw);
        std::filesystem::rename(bin_temp, bin);
        own_bin_temp = false;
        own_bin = true;
        std::ofstream manifest(json_temp, std::ios::binary | std::ios::trunc);
        own_json_temp = true;
        if (!manifest) throw std::runtime_error("cannot create metadata temporary file");
        manifest.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));
        Finish(manifest);
        std::filesystem::rename(json_temp, json);
        own_json_temp = false;
        result.written = true;
    } catch (const std::exception& e) {
        result.error = e.what();
        std::error_code ignored;
        if (own_bin_temp) std::filesystem::remove(bin_temp, ignored);
        if (own_json_temp) std::filesystem::remove(json_temp, ignored);
        if (own_bin) std::filesystem::remove(bin, ignored);
    }
    return result;
}
} // namespace Libs::Graphics::ImageDump::Raw
