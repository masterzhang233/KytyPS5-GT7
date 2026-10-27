#include "loader/gt7Profile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
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

#pragma pack(push, 1)
struct SelfHeader {
    uint8_t ident[12];
    uint16_t size1;
    uint16_t size2;
    uint64_t file_size;
    uint16_t segments_num;
    uint16_t unknown;
    uint32_t pad;
};
struct SelfSegment {
    uint64_t type;
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t decompressed_size;
};
struct ElfHeader {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_names;
};
struct ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};
#pragma pack(pop)

static_assert(sizeof(SelfHeader) == 32);
static_assert(sizeof(SelfSegment) == 32);
static_assert(sizeof(ElfHeader) == 64);
static_assert(sizeof(ProgramHeader) == 56);

constexpr uint32_t ElfLoad = 1;
constexpr uint16_t ElfX64 = 62;
constexpr uint16_t ElfDynExec = 0xfe10;
constexpr uint16_t ElfDynamic = 0xfe18;
constexpr size_t MaxSelfSegments = 4096;
constexpr size_t MaxProgramHeaders = 512;

struct ImageSegment {
    uint32_t flags = 0;
    uint64_t rva = 0;
    uint64_t file_offset = 0;
    uint64_t bytes = 0;
    uint64_t memory_size = 0;
    uint64_t alignment = 0;
};

struct ExecutableImage {
    uint64_t entry = 0;
    std::vector<ImageSegment> load_segments;
};

bool InRange(uint64_t total, uint64_t offset, uint64_t bytes) {
    return offset <= total && bytes <= total - offset;
}

template <typename T>
bool ReadAt(std::ifstream& file, uint64_t file_size, uint64_t offset, T* value) {
    if (!InRange(file_size, offset, sizeof(T)) ||
        offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(value), sizeof(T));
    return file.gcount() == sizeof(T);
}

bool IsElf(const uint8_t* ident) {
    return ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

bool IsKnownSelf(const SelfHeader& self) {
    const bool magic =
        (self.ident[0] == 0x4f && self.ident[1] == 0x15 && self.ident[2] == 0x3d && self.ident[3] == 0x1d) ||
        (self.ident[0] == 0x54 && self.ident[1] == 0x14 && self.ident[2] == 0xf5 && self.ident[3] == 0xee);
    const bool tail =
        (self.ident[4] == 0x00 && self.ident[5] == 0x01 && self.ident[6] == 0x01 && self.ident[7] == 0x12 &&
         self.ident[8] == 0x01 && self.ident[9] == 0x01 && self.ident[10] == 0x00 && self.ident[11] == 0x00 &&
         self.unknown == 0x22) ||
        (self.ident[4] == 0x10 && self.ident[5] == 0x01 && self.ident[6] == 0x01 && self.ident[7] == 0x12 &&
         self.ident[8] == 0x01 && self.ident[9] == 0x01 && self.ident[10] == 0x00 && self.ident[11] == 0x10 &&
         self.unknown == 0x32);
    return magic && tail;
}

bool InspectExecutable(const std::filesystem::path& path, ExecutableImage& image,
                       std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "cannot read executable image");
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) return Fail(error, "cannot determine executable image size");
    const uint64_t file_size = static_cast<uint64_t>(end);
    if (file_size < sizeof(ElfHeader)) return Fail(error, "executable image is truncated");

    std::array<uint8_t, 4> magic{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(magic.data()), magic.size());
    if (file.gcount() != magic.size()) return Fail(error, "cannot read executable image header");

    uint64_t elf_offset = 0;
    bool self = false;
    std::vector<SelfSegment> self_segments;
    if (!IsElf(magic.data())) {
        SelfHeader header{};
        if (!ReadAt(file, file_size, 0, &header) || !IsKnownSelf(header))
            return Fail(error, "unsupported executable container (expected a PS5 SELF or ELF)");
        if (header.segments_num == 0 || header.segments_num > MaxSelfSegments)
            return Fail(error, "invalid SELF segment table");
        elf_offset = sizeof(SelfHeader) + static_cast<uint64_t>(header.segments_num) * sizeof(SelfSegment);
        if (!InRange(file_size, sizeof(SelfHeader),
                     static_cast<uint64_t>(header.segments_num) * sizeof(SelfSegment)) ||
            header.file_size > file_size)
            return Fail(error, "SELF segment table is outside the executable");
        self_segments.resize(header.segments_num);
        for (size_t index = 0; index < self_segments.size(); ++index) {
            if (!ReadAt(file, file_size, sizeof(SelfHeader) + index * sizeof(SelfSegment),
                        &self_segments[index]))
                return Fail(error, "cannot read SELF segment table");
        }
        self = true;
    }

    ElfHeader elf{};
    if (!ReadAt(file, file_size, elf_offset, &elf) || !IsElf(elf.ident) || elf.ident[4] != 2 ||
        elf.ident[5] != 1 || elf.ident[6] != 1 || elf.ident[7] != 9 ||
        (elf.ident[8] != 0 && elf.ident[8] != 2) ||
        (elf.type != ElfDynExec && elf.type != ElfDynamic) || elf.machine != ElfX64 ||
        elf.version != 1 || elf.header_size != sizeof(ElfHeader) ||
        elf.program_entry_size != sizeof(ProgramHeader) || elf.program_count == 0 ||
        elf.program_count > MaxProgramHeaders)
        return Fail(error, "invalid PS5 ELF header");
    if (elf.program_offset > std::numeric_limits<uint64_t>::max() - elf_offset ||
        static_cast<uint64_t>(elf.program_count) >
            std::numeric_limits<uint64_t>::max() / sizeof(ProgramHeader))
        return Fail(error, "invalid ELF program-header table");
    const uint64_t programs_offset = elf_offset + elf.program_offset;
    const uint64_t programs_bytes = static_cast<uint64_t>(elf.program_count) * sizeof(ProgramHeader);
    if (!InRange(file_size, programs_offset, programs_bytes))
        return Fail(error, "ELF program-header table is outside the executable");

    image.entry = elf.entry;
    image.load_segments.clear();
    for (size_t index = 0; index < elf.program_count; ++index) {
        ProgramHeader program{};
        if (!ReadAt(file, file_size, programs_offset + index * sizeof(ProgramHeader), &program))
            return Fail(error, "cannot read ELF program-header table");
        if (program.type != ElfLoad) continue;
        if (program.file_size == 0 || program.memory_size < program.file_size ||
            program.vaddr > std::numeric_limits<uint64_t>::max() - program.file_size)
            return Fail(error, "invalid PT_LOAD range");

        uint64_t payload_offset = program.offset;
        if (self) {
            const SelfSegment* match = nullptr;
            for (const auto& segment : self_segments) {
                if ((segment.type & 0x800u) == 0 || ((segment.type >> 20u) & 0xfffu) != index)
                    continue;
                if (match != nullptr) return Fail(error, "ambiguous SELF mapping for PT_LOAD segment");
                match = &segment;
            }
            if (match == nullptr || match->compressed_size != match->decompressed_size ||
                match->decompressed_size != program.file_size)
                return Fail(error, "unsupported compressed or missing SELF PT_LOAD segment");
            payload_offset = match->offset;
        }
        if (!InRange(file_size, payload_offset, program.file_size))
            return Fail(error, "PT_LOAD bytes are outside the executable");
        image.load_segments.push_back({program.flags, program.vaddr, payload_offset,
                                       program.file_size, program.memory_size,
                                       program.alignment});
    }
    if (image.load_segments.empty()) return Fail(error, "executable has no PT_LOAD segments");
    for (size_t left = 0; left < image.load_segments.size(); ++left) {
        const auto& a = image.load_segments[left];
        for (size_t right = left + 1; right < image.load_segments.size(); ++right) {
            const auto& b = image.load_segments[right];
            if (a.rva < b.rva + b.bytes && b.rva < a.rva + a.bytes)
                return Fail(error, "overlapping PT_LOAD virtual ranges");
        }
    }
    return true;
}

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
bool ReadOriginal(std::ifstream& file, const ExecutableImage& image, uint64_t rva,
                  const std::vector<uint8_t>& expected) {
    uint64_t offset = 0;
    bool found = false;
    for (const auto& segment : image.load_segments) {
        if (rva >= segment.rva && rva - segment.rva <= segment.bytes &&
            expected.size() <= segment.bytes - (rva - segment.rva)) {
            offset = segment.file_offset + (rva - segment.rva);
            found = true;
            break;
        }
    }
    if (!found) return false;
    std::vector<uint8_t> actual(expected.size());
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    return file.good() && actual == expected;
}

#ifdef _WIN32
class Sha256 {
public:
    ~Sha256() {
        if (value_ != nullptr) BCryptDestroyHash(value_);
        if (algorithm_ != nullptr) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    bool Open(std::string& error) {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptCreateHash(algorithm_, &value_, nullptr, 0, nullptr, 0, 0) < 0)
            return Fail(error, "Windows SHA-256 provider initialization failed");
        return true;
    }
    bool Add(const void* data, size_t bytes, std::string& error) {
        if (bytes > std::numeric_limits<ULONG>::max() ||
            BCryptHashData(value_, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data)),
                           static_cast<ULONG>(bytes), 0) < 0)
            return Fail(error, "SHA-256 data hashing failed");
        return true;
    }
    bool Finish(std::string& digest, std::string& error) {
        std::array<uint8_t, 32> output{};
        if (BCryptFinishHash(value_, output.data(), static_cast<ULONG>(output.size()), 0) < 0)
            return Fail(error, "SHA-256 finalization failed");
        constexpr char digits[] = "0123456789abcdef";
        digest.clear();
        for (uint8_t byte : output) { digest += digits[byte >> 4]; digest += digits[byte & 15]; }
        return true;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE value_ = nullptr;
};

template <size_t Size>
void Put32(std::array<uint8_t, Size>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}
template <size_t Size>
void Put64(std::array<uint8_t, Size>& bytes, size_t offset, uint64_t value) {
    for (size_t index = 0; index < 8; ++index) bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

bool HashExecutableImage(const std::filesystem::path& path, const ExecutableImage& image,
                         std::string& digest, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "cannot read executable image for SHA-256 validation");
    Sha256 hash;
    if (!hash.Open(error)) return false;
    std::array<uint8_t, 16> image_metadata{};
    Put64(image_metadata, 0, image.entry);
    Put64(image_metadata, 8, static_cast<uint64_t>(image.load_segments.size()));
    if (!hash.Add(image_metadata.data(), image_metadata.size(), error)) return false;
    std::vector<uint8_t> buffer(1024 * 1024);
    for (const auto& segment : image.load_segments) {
        std::array<uint8_t, 40> metadata{};
        Put32(metadata, 0, ElfLoad);
        Put32(metadata, 4, segment.flags);
        Put64(metadata, 8, segment.rva);
        Put64(metadata, 16, segment.bytes);
        Put64(metadata, 24, segment.memory_size);
        Put64(metadata, 32, segment.alignment);
        if (!hash.Add(metadata.data(), metadata.size(), error)) return false;
        file.clear();
        file.seekg(static_cast<std::streamoff>(segment.file_offset));
        uint64_t remaining = segment.bytes;
        while (remaining != 0) {
            const auto bytes = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bytes));
            if (file.gcount() != static_cast<std::streamsize>(bytes))
                return Fail(error, "file read failed during executable-image SHA-256 validation");
            if (!hash.Add(buffer.data(), bytes, error)) return false;
            remaining -= bytes;
        }
    }
    return hash.Finish(digest, error);
}
#else
bool HashExecutableImage(const std::filesystem::path&, const ExecutableImage&, std::string&,
                         std::string& error) {
    return Fail(error, "the packaged GT7 render profiles currently support Windows only");
}
#endif
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
    Sha256 hash;
    if (!hash.Open(error)) return false;
    std::vector<uint8_t> buffer(1024 * 1024);
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (file.gcount() > 0 && !hash.Add(buffer.data(), static_cast<size_t>(file.gcount()), error))
            return false;
    }
    if (!file.eof()) return Fail(error, "file read failed during SHA-256 validation");
    return hash.Finish(digest, error);
#else
    return Fail(error, "the packaged GT7 render profiles currently support Windows only");
#endif
}

bool ExecutableImageSha256(const std::filesystem::path& path, std::string& digest,
                           std::string& error) {
#ifdef _WIN32
    ExecutableImage image;
    return InspectExecutable(path, image, error) && HashExecutableImage(path, image, digest, error);
#else
    return Fail(error, "the packaged GT7 render profiles currently support Windows only");
#endif
}

bool FileOffset(const std::filesystem::path& path, uint64_t rva, uint64_t size,
                uint64_t& offset, std::string& error) {
    if (size == 0) return Fail(error, "zero-byte executable range");
    ExecutableImage image;
    if (!InspectExecutable(path, image, error)) return false;
    for (const auto& segment : image.load_segments) {
        if (rva >= segment.rva && rva - segment.rva <= segment.bytes &&
            size <= segment.bytes - (rva - segment.rva)) {
            offset = segment.file_offset + (rva - segment.rva);
            return true;
        }
    }
    return Fail(error, "executable RVA is outside PT_LOAD file bytes");
}

bool Prepare(const Request& request, const std::filesystem::path& eboot,
             const std::filesystem::path& patch_directory, std::filesystem::path& patch,
             std::string& error) {
    if (!request.Active()) return true;
    try {
        if (!std::filesystem::is_regular_file(eboot))
            return Fail(error, "GT7 profiles require an extracted game directory or eboot.bin; archives are not supported");
        std::string file_digest;
        if (!Sha256File(eboot, file_digest, error)) return false;
        ExecutableImage image;
        std::string image_digest;
        if (!InspectExecutable(eboot, image, error) ||
            !HashExecutableImage(eboot, image, image_digest, error)) return false;
        if (image_digest != EbootImageSha256)
            return Fail(error, "unsupported eboot.bin: expected PPSA01317 01.710.000 executable-image SHA-256 " +
                std::string(EbootImageSha256) + ", got " + image_digest + " (file SHA-256 " +
                file_digest + "); nothing was patched");
        if (file_digest != EbootSha256)
            std::printf("GT7 profile: accepted compatible executable image from alternate container; file SHA-256 %s\n",
                        file_digest.c_str());
        const auto file_name = request.vr_scale ? "GT7-SteamVR-EyeScale-" + std::to_string(request.vr_scale) + ".json" :
            "GT7-Desktop-Internal-" + (request.desktop_height ? std::to_string(request.desktop_height) : std::string("Native")) + ".json";
        const auto selected = patch_directory / file_name;
        std::ifstream manifest_file(patch_directory / "patches.json");
        const auto manifest = nlohmann::json::parse(manifest_file);
        if (manifest.at("eboot_sha256") != EbootSha256 ||
            manifest.at("executable_image_sha256") != EbootImageSha256)
            return Fail(error, "invalid GT7 patch manifest");
        std::string expected_patch_hash;
        for (const auto& row : manifest.at("patches")) if (row.at("file") == file_name) expected_patch_hash = row.at("sha256");
        std::string patch_digest;
        if (expected_patch_hash.empty() || !Sha256File(selected, patch_digest, error) ||
            patch_digest != expected_patch_hash)
            return Fail(error, "GT7 patch file missing or checksum mismatch");
        std::ifstream patch_file(selected);
        const auto root = nlohmann::json::parse(patch_file);
        if (root.at("id") != "PPSA01317" || root.at("version") != "01.710.000" ||
            root.at("process") != "eboot.bin" || root.at("eboot_sha256") != EbootSha256 ||
            root.at("executable_image_sha256") != EbootImageSha256 ||
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
                    !ReadOriginal(original, image, rva, off))
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
