#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Loader::Gt7Profile {

// Whole-file digest of the audited reference SELF. It identifies the source artifact in
// manifests and diagnostics; compatibility is decided by EbootImageSha256 below.
inline constexpr const char* EbootSha256 =
    "9bef586d199c088b1e558333ba897507235dae2e148a9c6e3de93eedbb5f88bf";
// SHA-256 of the canonical PT_LOAD image (program-header order; each segment contributes
// p_type, p_flags, p_vaddr, p_filesz and its uncompressed bytes). Unlike EbootSha256 this
// is independent of whether the executable is stored as a SELF or as its extracted ELF.
inline constexpr const char* EbootImageSha256 =
    "0cf94d196a1d3e04bd80c9fb8bfe3ee5becf69aadfa558d416c796c4335babd6";

struct Request {
    int desktop_height = -1; // -1: not requested; 0: native
    int vr_scale = 0;        // 0: not requested
    bool check_only = false;
    [[nodiscard]] bool Active() const { return desktop_height >= 0 || vr_scale != 0; }
};

// No guest initialization, writes, cache access, runtime loading, or GPU calls.
bool ValidateRequest(const Request& request, bool vr, bool custom_patch, std::string& error);
bool Sha256File(const std::filesystem::path& path, std::string& digest, std::string& error);
bool ExecutableImageSha256(const std::filesystem::path& path, std::string& digest,
                           std::string& error);
bool FileOffset(const std::filesystem::path& path, uint64_t rva, uint64_t size,
                uint64_t& offset, std::string& error);
bool Prepare(const Request& request, const std::filesystem::path& eboot,
             const std::filesystem::path& patch_directory, std::filesystem::path& patch,
             std::string& error);

} // namespace Loader::Gt7Profile
