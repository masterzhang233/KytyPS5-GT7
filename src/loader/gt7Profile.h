#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Loader::Gt7Profile {

inline constexpr const char* EbootSha256 =
    "9bef586d199c088b1e558333ba897507235dae2e148a9c6e3de93eedbb5f88bf";

struct Request {
    int desktop_height = -1; // -1: not requested; 0: native
    int vr_scale = 0;        // 0: not requested
    bool check_only = false;
    [[nodiscard]] bool Active() const { return desktop_height >= 0 || vr_scale != 0; }
};

// No guest initialization, writes, cache access, runtime loading, or GPU calls.
bool ValidateRequest(const Request& request, bool vr, bool custom_patch, std::string& error);
bool Sha256File(const std::filesystem::path& path, std::string& digest, std::string& error);
bool FileOffset(uint64_t rva, uint64_t size, uint64_t& offset);
bool Prepare(const Request& request, const std::filesystem::path& eboot,
             const std::filesystem::path& patch_directory, std::filesystem::path& patch,
             std::string& error);

} // namespace Loader::Gt7Profile
