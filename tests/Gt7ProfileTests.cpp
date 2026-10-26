#include "loader/gt7Profile.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
	using namespace Loader::Gt7Profile;
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

	uint64_t file_offset = 0;
	Require(FileOffset(0x1173f1, 10, file_offset) && file_offset == 0x202f0 + 0x1173f1,
	        "first SELF segment translation failed");
	Require(!FileOffset(0x3f0e72b, 2, file_offset), "cross-segment patch accepted");
	Require(!FileOffset(0x3f0e72c, 1, file_offset), "segment-end patch accepted");
	Require(!FileOffset(0x1173f1, 0, file_offset), "zero-byte patch accepted");

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
	return 0;
}
