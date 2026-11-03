#include "graphics/host_gpu/renderer/image/rawImageDump.h"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace Raw = Libs::Graphics::ImageDump::Raw;
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
Raw::Layout Fixture() {
    Raw::Layout l;
    l.guest_address = 0x1234567890;
    l.guest_bytes = 4096;
    l.capture = 2;
    l.operation = 43;
    l.content_serial = 7;
    l.vk_format = 100; // VK_FORMAT_R32_SFLOAT; helper deliberately does not decode it.
    l.guest_width = l.host_width = 4;
    l.guest_height = l.host_height = 6;
    l.guest_depth = l.host_depth = 1;
    l.host_layers = l.available_tiles = 3;
    l.host_mips = 3;
    l.mip = 1;
    l.width = 2;
    l.height = 3;
    l.rows = 2;
    l.row_step = 2;
    l.tiles = 2;
    l.texel_bytes = 4;
    l.row_pitch = 16;
    l.tile_pitch = 48;
    return l;
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("kyty-raw-dump-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto l = Fixture();
        std::vector<uint8_t> bytes(96, 0xcc), packed;
        // Negative float, signed zero, and NaN payload must retain every original bit.
        const std::array<uint32_t, 8> values{0xbf800000, 0x7fc01234, 0x80000000, 0x3f800000,
                                            0xff800000, 0x7f800000, 0x00000001, 0x12345678};
        for (uint32_t t = 0; t < 2; ++t) {
            for (uint32_t y = 0; y < 2; ++y) {
                const auto* begin = reinterpret_cast<const uint8_t*>(values.data() + t * 4 + y * 2);
                std::memcpy(bytes.data() + t * 48 + y * 16, begin, 8);
                packed.insert(packed.end(), begin, begin + 8);
            }
        }
        uint64_t count = 0;
        std::string error;
        Require(Raw::Validate(l, bytes.size(), count, error) && count == 32, "valid layout rejected");
        Require(!Raw::Validate(l, 71, count, error), "truncated final row accepted");
        auto invalid = l;
        invalid.row_pitch = 7;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "overlapping rows accepted");
        invalid = l;
        invalid.tile_pitch = 16;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "overlapping tiles accepted");
        invalid = l;
        invalid.row_pitch = invalid.tile_pitch = std::numeric_limits<uint64_t>::max();
        Require(!Raw::Validate(invalid, std::numeric_limits<uint64_t>::max(), count, error), "overflow accepted");
        invalid = l;
        invalid.rows = 1;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "inconsistent row selection accepted");
        invalid = l;
        invalid.texel_bytes = 0;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "zero texel size accepted");
        invalid = l;
        invalid.mip = invalid.host_mips;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "invalid mip accepted");
        invalid = l;
        invalid.host_mips = 33;
        invalid.mip = 32;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "unsafe mip shift accepted");
        invalid = l;
        invalid.host_width = 8;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "inconsistent mip width accepted");
        invalid = l;
        invalid.available_tiles = 4;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "inconsistent layer count accepted");
        invalid = l;
        invalid.tiles = 4;
        Require(!Raw::Validate(invalid, bytes.size(), count, error), "too many tiles accepted");

        Raw::Writer writer(1 << 20);
        auto result = writer.Write(directory, "sample", "capture with \"quotes\"\n", l, bytes);
        Require(result.written && result.payload_bytes == packed.size(), "write failed");
        Require(Read(directory / "sample.bin") == packed, "texels changed or GPU padding leaked");
        std::ifstream metadata_file(directory / "sample.json");
        const auto metadata = nlohmann::json::parse(metadata_file);
        Require(metadata.at("complete") == true, "completion marker missing");
        Require(metadata.at("semantic") == "unclassified", "semantics were guessed");
        Require(metadata["selection"]["mip"] == 1 &&
                metadata["selection"]["source_y_step"] == 2 &&
                metadata["selection"]["tiles_truncated"] == true &&
                metadata["selection"]["complete_texture"] == false, "selection metadata wrong");
        Require(metadata["storage"]["row_pitch"] == 8 &&
                metadata["storage"]["tile_pitch"] == 16 &&
                metadata["storage"]["padding_included"] == false, "packed layout wrong");
        metadata_file.close();
        Require(!std::filesystem::exists(directory / "sample.bin.part") &&
                !std::filesystem::exists(directory / "sample.json.part"), "unfinished temporaries remain");
        result = writer.Write(directory, "sample", "overwrite", l, bytes);
        Require(!result.written && Read(directory / "sample.bin") == packed, "existing capture overwritten");
        Require(!writer.Write(directory, "../escape", "", l, bytes).written, "path traversal accepted");
        Raw::Writer exhausted(0);
        Require(!exhausted.Write(directory, "no-budget", "", l, bytes).written &&
                !std::filesystem::exists(directory / "no-budget.json"), "budget failure published success");
        const auto charge = std::filesystem::file_size(directory / "sample.bin") +
                            std::filesystem::file_size(directory / "sample.json");
        Raw::Writer one_capture(charge + 128);
        Require(one_capture.Write(directory, "budget-one", "", l, bytes).written, "budget fixture failed");
        Require(!one_capture.Write(directory, "budget-two", "", l, bytes).written,
                "process budget was reset for each capture");
        { std::ofstream blocker(directory / "not-a-directory"); blocker << "block"; }
        Require(!writer.Write(directory / "not-a-directory", "bad", "", l, bytes).written,
                "filesystem failure reported success");
        Require(!std::filesystem::exists(directory / "not-a-directory" / "bad.json"),
                "failed output has completed metadata");
        std::filesystem::remove_all(directory);
        std::cout << "RawImageDumpTests: all checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RawImageDumpTests: " << e.what() << " (artifacts: " << directory << ")\n";
        return 1;
    }
}
