#include "loader/gt7Profile.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

#pragma pack(push, 1)
struct TestSelfHeader {
	uint8_t ident[12];
	uint16_t size1;
	uint16_t size2;
	uint64_t file_size;
	uint16_t segments_num;
	uint16_t unknown;
	uint32_t pad;
};
struct TestSelfSegment {
	uint64_t type;
	uint64_t offset;
	uint64_t compressed_size;
	uint64_t decompressed_size;
};
struct TestElfHeader {
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
struct TestProgramHeader {
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

template <typename T>
void Put(std::vector<uint8_t>& bytes, size_t offset, const T& value) {
	Require(offset <= bytes.size() && sizeof(value) <= bytes.size() - offset,
	        "synthetic executable write is out of range");
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void PutBytes(std::vector<uint8_t>& bytes, size_t offset, const void* value, size_t size) {
	Require(offset <= bytes.size() && size <= bytes.size() - offset,
	        "synthetic executable payload is out of range");
	std::memcpy(bytes.data() + offset, value, size);
}

enum class FixtureMutation { None, Entry, MemorySize, Alignment, Payload };

std::filesystem::path WriteExecutableFixture(const std::filesystem::path& path, bool self,
	                                           bool compressed = false,
	                                           FixtureMutation mutation = FixtureMutation::None) {
	constexpr std::array<uint8_t, 5> first {'a', 'b', 'c', 'd', 'e'};
	constexpr std::array<uint8_t, 4> second {1, 2, 3, 4};
	const size_t elf_offset = self ? sizeof(TestSelfHeader) + 2 * sizeof(TestSelfSegment) : 0;
	std::vector<uint8_t> bytes(self ? 0x500 : 0x400);

	TestElfHeader elf {};
	const uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 9, 2};
	std::memcpy(elf.ident, ident, sizeof(ident));
	elf.type               = 0xfe10;
	elf.machine            = 62;
	elf.version            = 1;
	elf.entry              = mutation == FixtureMutation::Entry ? 0x1001 : 0x1000;
	elf.program_offset     = sizeof(TestElfHeader);
	elf.header_size        = sizeof(TestElfHeader);
	elf.program_entry_size = sizeof(TestProgramHeader);
	elf.program_count      = 2;
	Put(bytes, elf_offset, elf);

	TestProgramHeader program0 {.type        = 1,
	                            .flags       = 1,
	                            .offset      = 0x200,
	                            .vaddr       = 0x1000,
	                            .file_size   = first.size(),
	                            .memory_size = first.size() +
	                                           (mutation == FixtureMutation::MemorySize ? 1 : 0),
	                            .alignment   = mutation == FixtureMutation::Alignment
	                                               ? uint64_t {0x2000}
	                                               : uint64_t {0x1000}};
	TestProgramHeader program1 {.type        = 1,
	                            .flags       = 6,
	                            .offset      = 0x300,
	                            .vaddr       = 0x3000,
	                            .file_size   = second.size(),
	                            .memory_size = 8,
	                            .alignment   = 0x1000};
	Put(bytes, elf_offset + sizeof(TestElfHeader), program0);
	Put(bytes, elf_offset + sizeof(TestElfHeader) + sizeof(TestProgramHeader), program1);

	if (self) {
		TestSelfHeader header {};
		const uint8_t self_ident[] = {0x54, 0x14, 0xf5, 0xee, 0x00, 0x01,
		                              0x01, 0x12, 0x01, 0x01, 0x00, 0x00};
		std::memcpy(header.ident, self_ident, sizeof(self_ident));
		header.file_size    = bytes.size();
		header.segments_num = 2;
		header.unknown      = 0x22;
		Put(bytes, 0, header);
		TestSelfSegment segment0 {.type              = 0x2804,
		                          .offset            = 0x300,
		                          .compressed_size   = first.size() - (compressed ? 1 : 0),
		                          .decompressed_size = first.size()};
		TestSelfSegment segment1 {.type              = 0x102804,
		                          .offset            = 0x400,
		                          .compressed_size   = second.size(),
		                          .decompressed_size = second.size()};
		Put(bytes, sizeof(TestSelfHeader), segment0);
		Put(bytes, sizeof(TestSelfHeader) + sizeof(TestSelfSegment), segment1);
		PutBytes(bytes, 0x300, first.data(), first.size());
		PutBytes(bytes, 0x400, second.data(), second.size());
	} else {
		PutBytes(bytes, 0x200, first.data(), first.size());
		PutBytes(bytes, 0x300, second.data(), second.size());
	}
	if (mutation == FixtureMutation::Payload) bytes[(self ? 0x300 : 0x200) + 1] ^= 1;

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(stream.good(), "could not create executable fixture");
	return path;
}
} // namespace

int main(int argc, char* argv[]) {
	using namespace Loader::Gt7Profile;
	std::filesystem::path patch_directory;
	std::vector<std::filesystem::path> executables;
	bool has_patch_directory = false;
	for (int index = 1; index < argc; ++index) {
		if (std::string(argv[index]) == "--patch-dir") {
			Require(!has_patch_directory && index + 1 < argc,
			        "--patch-dir requires one path and must not be repeated");
			patch_directory = argv[++index];
			has_patch_directory = true;
		} else {
			executables.emplace_back(argv[index]);
		}
	}
	Require(!has_patch_directory || !executables.empty(),
	        "--patch-dir requires at least one real executable argument");

	Require(IsSupportedExecutableImage(EbootImageSha256), "reference image identity rejected");
	Require(IsSupportedExecutableImage(EbootAlternateImageSha256), "audited alternate image identity rejected");
	for (const auto& supported : {std::string(EbootImageSha256), std::string(EbootAlternateImageSha256)}) {
		Require(!IsSupportedExecutableImage(supported.substr(1)), "truncated image identity accepted");
		Require(!IsSupportedExecutableImage(supported + "0"), "oversized image identity accepted");
		Require(!IsSupportedExecutableImage(" " + supported), "whitespace-prefixed image identity accepted");
		std::string changed = supported;
		changed[0] = changed[0] == '0' ? '1' : '0';
		Require(!IsSupportedExecutableImage(changed), "modified image identity accepted");
		changed = supported;
		for (auto& character : changed) {
			if (character >= 'a' && character <= 'f') character -= 'a' - 'A';
		}
		Require(!IsSupportedExecutableImage(changed), "case-altered image identity accepted");
	}
	Require(!IsSupportedExecutableImage(""), "empty image identity accepted");
	Require(!IsSupportedExecutableImage(std::string(64, '0')), "unknown image identity accepted");
	Require(!IsSupportedExecutableImage(std::string(64, 'g')), "non-hex image identity accepted");

	std::string error;
	Request desktop{.desktop_height = 1440};
	Require(ValidateRequest(desktop, false, false, error), "desktop request rejected");
	Require(!ValidateRequest(desktop, true, false, error), "desktop/VR conflict accepted");
	Require(!ValidateRequest(desktop, false, true, error), "custom patch conflict accepted");
	Request vr{.vr_scale = 100};
	Require(ValidateRequest(vr, true, false, error), "VR request rejected");
	Require(!ValidateRequest(vr, false, false, error), "VR scale without VR accepted");
	Request invalid{.desktop_height = 720};
	Require(!ValidateRequest(invalid, false, false, error), "unsupported desktop height accepted");
	Request check_only{.check_only = true};
	Require(!ValidateRequest(check_only, false, false, error), "empty preflight accepted");

	const auto sample = std::filesystem::temp_directory_path() / "kyty_gt7_profile_sha256_test.bin";
	{
		std::ofstream stream(sample, std::ios::binary | std::ios::trunc);
		stream << "abc";
		Require(stream.good(), "could not create SHA-256 fixture");
	}
	std::string digest;
	Require(Sha256File(sample, digest, error), "could not hash SHA-256 fixture");
	Require(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	        "SHA-256 digest mismatch");
	std::filesystem::path patch = "untouched";
	Require(!Prepare(desktop, sample, {}, patch, error) && patch == "untouched",
	        "unsupported eboot was accepted or output patch changed");
	std::filesystem::remove(sample);

	const auto plain = std::filesystem::temp_directory_path() / "kyty_gt7_profile_plain_elf_test.bin";
	const auto self  = std::filesystem::temp_directory_path() / "kyty_gt7_profile_self_test.bin";
	const auto compressed =
	    std::filesystem::temp_directory_path() / "kyty_gt7_profile_compressed_self_test.bin";
	const auto changed_entry =
	    std::filesystem::temp_directory_path() / "kyty_gt7_profile_changed_entry_test.bin";
	const auto changed_memory_size =
	    std::filesystem::temp_directory_path() / "kyty_gt7_profile_changed_memsz_test.bin";
	const auto changed_alignment =
	    std::filesystem::temp_directory_path() / "kyty_gt7_profile_changed_align_test.bin";
	const auto changed_payload =
	    std::filesystem::temp_directory_path() / "kyty_gt7_profile_changed_payload_test.bin";
	WriteExecutableFixture(plain, false);
	WriteExecutableFixture(self, true);
	WriteExecutableFixture(compressed, true, true);
	WriteExecutableFixture(changed_entry, false, false, FixtureMutation::Entry);
	WriteExecutableFixture(changed_memory_size, false, false, FixtureMutation::MemorySize);
	WriteExecutableFixture(changed_alignment, false, false, FixtureMutation::Alignment);
	WriteExecutableFixture(changed_payload, false, false, FixtureMutation::Payload);
	std::string plain_digest;
	std::string self_digest;
	Require(ExecutableImageSha256(plain, plain_digest, error), "plain ELF image hashing failed");
	Require(ExecutableImageSha256(self, self_digest, error), "SELF image hashing failed");
	Require(plain_digest == "946e022841d698fde7598cd8ceecaec32c4c61053f25018aa3ae023e62ba7aa2",
	        "synthetic executable-image digest changed");
	Require(self_digest == plain_digest, "SELF and plain ELF image identities differ");
	Require(!IsSupportedExecutableImage(plain_digest), "unrecognized synthetic image identity accepted");
	Require(!Prepare(desktop, plain, {}, patch, error) && patch == "untouched" &&
	        error.find("unsupported eboot.bin") != std::string::npos,
	        "valid ELF with an unsupported image identity was accepted or changed output patch");
	for (const auto& changed : {changed_entry, changed_memory_size, changed_alignment, changed_payload}) {
		std::string changed_digest;
		Require(ExecutableImageSha256(changed, changed_digest, error),
		        "changed ELF image hashing failed");
		Require(changed_digest != plain_digest,
		        "ELF load semantics or bytes changed without changing executable-image identity");
	}
	uint64_t file_offset = 0;
	Require(FileOffset(plain, 0x1002, 2, file_offset, error) && file_offset == 0x202,
	        "plain ELF RVA translation failed");
	Require(FileOffset(self, 0x1002, 2, file_offset, error) && file_offset == 0x302,
	        "SELF RVA translation failed");
	Require(!FileOffset(self, 0x1004, 2, file_offset, error), "cross-segment patch accepted");
	Require(!FileOffset(self, 0x1000, 0, file_offset, error), "zero-byte patch accepted");
	Require(!ExecutableImageSha256(compressed, digest, error), "compressed SELF was accepted");
	std::filesystem::remove(plain);
	std::filesystem::remove(self);
	std::filesystem::remove(compressed);
	std::filesystem::remove(changed_entry);
	std::filesystem::remove(changed_memory_size);
	std::filesystem::remove(changed_alignment);
	std::filesystem::remove(changed_payload);

	// Real game files are supplied locally, never embedded in these tests or the repository.
	// With --patch-dir, exercise the same preflight used by every packaged preset.
	struct ProfileCase {
		Request request;
		const char* file_name;
	};
	const std::array<ProfileCase, 8> profiles {{
	    {{.desktop_height = 0}, "GT7-Desktop-Internal-Native.json"},
	    {{.desktop_height = 1080}, "GT7-Desktop-Internal-1080.json"},
	    {{.desktop_height = 1440}, "GT7-Desktop-Internal-1440.json"},
	    {{.desktop_height = 2160}, "GT7-Desktop-Internal-2160.json"},
	    {{.vr_scale = 100}, "GT7-SteamVR-EyeScale-100.json"},
	    {{.vr_scale = 110}, "GT7-SteamVR-EyeScale-110.json"},
	    {{.vr_scale = 120}, "GT7-SteamVR-EyeScale-120.json"},
	    {{.vr_scale = 125}, "GT7-SteamVR-EyeScale-125.json"},
	}};
	for (const auto& executable : executables) {
		Require(ExecutableImageSha256(executable, digest, error),
		        "real executable image could not be inspected");
		Require(IsSupportedExecutableImage(digest), "real executable image digest is not audited");
		if (!has_patch_directory) continue;
		std::string original_digest;
		Require(Sha256File(executable, original_digest, error), "could not hash real executable before preflight");
		for (const auto& profile : profiles) {
			std::filesystem::path selected = "untouched";
			const bool prepared = Prepare(profile.request, executable, patch_directory, selected, error);
			Require(prepared, (std::string("real executable preflight failed for ") +
			                  profile.file_name + ": " + error).c_str());
			Require(selected == patch_directory / profile.file_name, "preflight selected an unexpected patch");
		}
		Require(Sha256File(executable, digest, error) && digest == original_digest,
		        "preflight modified the real executable");
	}
	return 0;
}
