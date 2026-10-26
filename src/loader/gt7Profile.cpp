#include "loader/gt7Profile.h"

#include <array>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace Loader::Gt7Profile {
namespace {
bool Fail(std::string& error, const std::string& message) { error = message; return false; }
bool Hex(const std::string& text, std::vector<uint8_t>& bytes) {
    if (text.empty() || text.size() % 2 != 0 || text.size() > 4096) return false;
    bytes.clear();
    for (size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        const auto [end, code] = std::from_chars(text.data()+i, text.data()+i+2, value, 16);
        if (code != std::errc{} || end != text.data()+i+2) return false;
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return true;
}
bool ReadOriginal(std::ifstream& file, uint64_t rva, const std::vector<uint8_t>& expected) {
    uint64_t offset = 0;
    if (!FileOffset(rva, expected.size(), offset)) return false;
    std::vector<uint8_t> actual(expected.size());
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    return file.good() && actual == expected;
}
} // namespace

bool ValidateRequest(const Request& request, bool vr, bool custom_patch, std::string& error) {
    if (request.desktop_height != -1 && request.desktop_height != 0 &&
        request.desktop_height != 1080 && request.desktop_height != 1440 && request.desktop_height != 2160)
        return Fail(error, "desktop height must be native, 1080, 1440, or 2160");
    if (request.vr_scale != 0 && request.vr_scale != 100 && request.vr_scale != 110 &&
        request.vr_scale != 120 && request.vr_scale != 125)
        return Fail(error, "VR scale must be 100, 110, 120, or 125");
    if (request.desktop_height >= 0 && (vr || request.vr_scale != 0))
        return Fail(error, "desktop render profiles and VR cannot be combined");
    if (request.vr_scale != 0 && !vr) return Fail(error, "--gt7-vr-scale requires --vr");
    if (request.Active() && custom_patch) return Fail(error, "GT7 profiles cannot be combined with --game-patch");
    if (request.check_only && !request.Active()) return Fail(error, "--gt7-check-only requires a GT7 profile");
    return true;
}

bool Sha256File(const std::filesystem::path& path, std::string& digest, std::string& error) {
#ifdef _WIN32
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "cannot read file for SHA-256 validation");
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() { if (value) BCryptDestroyHash(value); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0)
        return Fail(error, "Windows SHA-256 provider initialization failed");
    std::vector<uint8_t> buffer(1024 * 1024);
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (file.gcount() > 0 && BCryptHashData(hash.value, buffer.data(), static_cast<ULONG>(file.gcount()), 0) < 0)
            return Fail(error, "SHA-256 data hashing failed");
    }
    if (!file.eof()) return Fail(error, "file read failed during SHA-256 validation");
    std::array<uint8_t, 32> output{};
    if (BCryptFinishHash(hash.value, output.data(), static_cast<ULONG>(output.size()), 0) < 0)
        return Fail(error, "SHA-256 finalization failed");
    constexpr char digits[] = "0123456789abcdef";
    digest.clear();
    for (uint8_t byte : output) { digest += digits[byte >> 4]; digest += digits[byte & 15]; }
    return true;
#else
    return Fail(error, "the packaged GT7 render profiles currently support Windows only");
#endif
}

bool FileOffset(uint64_t rva, uint64_t size, uint64_t& offset) {
    // Verified SELF segments for EbootSha256 only. Callers must verify that hash first.
    struct Segment { uint64_t rva, offset, bytes; };
    constexpr Segment segments[] = {{0,0x202f0,0x3f0e72c}, {0x3f10000,0x3f37560,0x11660f0},
        {0x5078000,0x50a01d0,0x56ccd8}, {0x55e8000,0x5610850,0x732ee8}};
    for (const auto& segment : segments) {
        if (size && rva >= segment.rva && rva-segment.rva <= segment.bytes &&
            size <= segment.bytes-(rva-segment.rva)) {
            offset = segment.offset + (rva-segment.rva);
            return true;
        }
    }
    return false;
}

bool Prepare(const Request& request, const std::filesystem::path& eboot,
             const std::filesystem::path& patch_directory, std::filesystem::path& patch,
             std::string& error) {
    if (!request.Active()) return true;
    try {
        if (!std::filesystem::is_regular_file(eboot))
            return Fail(error, "GT7 profiles require an extracted game directory or eboot.bin; archives are not supported");
        std::string digest;
        if (!Sha256File(eboot, digest, error)) return false;
        if (digest != EbootSha256)
            return Fail(error, "unsupported eboot.bin: expected PPSA01317 01.710.000 SHA-256 " +
                std::string(EbootSha256) + ", got " + digest + "; nothing was patched");
        const auto file_name = request.vr_scale ? "GT7-SteamVR-EyeScale-" + std::to_string(request.vr_scale) + ".json" :
            "GT7-Desktop-Internal-" + (request.desktop_height ? std::to_string(request.desktop_height) : std::string("Native")) + ".json";
        const auto selected = patch_directory / file_name;
        std::ifstream manifest_file(patch_directory / "patches.json");
        const auto manifest = nlohmann::json::parse(manifest_file);
        if (manifest.at("eboot_sha256") != EbootSha256) return Fail(error, "invalid GT7 patch manifest");
        std::string expected_patch_hash;
        for (const auto& row : manifest.at("patches")) if (row.at("file") == file_name) expected_patch_hash = row.at("sha256");
        if (expected_patch_hash.empty() || !Sha256File(selected, digest, error) || digest != expected_patch_hash)
            return Fail(error, "GT7 patch file missing or checksum mismatch");
        std::ifstream patch_file(selected);
        const auto root = nlohmann::json::parse(patch_file);
        if (root.at("id") != "PPSA01317" || root.at("version") != "01.710.000" ||
            root.at("process") != "eboot.bin" || root.at("eboot_sha256") != EbootSha256 ||
            !root.at("strict_main_module").get<bool>() || root.at("main_module_source_base").get<uint64_t>() != 0)
            return Fail(error, "GT7 patch identity or strict main-module requirements do not match");
        std::ifstream original(eboot, std::ios::binary);
        for (const auto& mod : root.at("mods")) {
            if (!mod.value("enabled", true)) continue;
            for (const auto& entry : mod.at("memory")) {
                const std::string address = entry.at("offset");
                uint64_t rva = 0;
                const auto [end, code] = std::from_chars(address.data(), address.data()+address.size(), rva, 16);
                std::vector<uint8_t> off, on;
                if (code != std::errc{} || end != address.data()+address.size() ||
                    !Hex(entry.at("off"), off) || !Hex(entry.at("on"), on) || off.size() != on.size() ||
                    !ReadOriginal(original, rva, off))
                    return Fail(error, "GT7 patch original-byte or segment-range validation failed");
            }
        }
        patch = selected;
        return true;
    } catch (const std::exception& exception) {
        return Fail(error, std::string("GT7 profile validation failed: ") + exception.what());
    }
}
} // namespace Loader::Gt7Profile
