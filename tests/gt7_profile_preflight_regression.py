#!/usr/bin/env python3
"""Exercise the packaged GT7 preflight; local game samples are never distributed.

Every emulator invocation includes --gt7-check-only. A disposable runtime copy
contains all negative fixtures; the supplied runtime and game files stay intact.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time


VERIFIED = "verified executable image and original patch bytes"
PROFILES = [
    (f"desktop-{height}", ["--gt7-desktop-height", height],
     f"GT7-Desktop-Internal-{'Native' if height == 'native' else height}.json")
    for height in ("native", "1080", "1440", "2160")
] + [
    (f"vr-{scale}", ["--vr", "--gt7-vr-scale", scale],
     f"GT7-SteamVR-EyeScale-{scale}.json")
    for scale in ("100", "110", "120", "125")
]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def load_segments(path):
    """Return (RVA, payload file offset, size) for uncompressed ELF/SELF PT_LOADs."""
    total = path.stat().st_size
    with path.open("rb") as stream:
        def unpack(offset, layout):
            size = struct.calcsize(layout)
            if offset < 0 or offset + size > total:
                raise ValueError("Executable header range is outside the input")
            stream.seek(offset)
            return struct.unpack(layout, stream.read(size))

        magic = unpack(0, "4s")[0]
        elf_offset, self_segments = 0, []
        if magic != b"\x7fELF":
            if magic not in (b"\x4f\x15\x3d\x1d", b"\x54\x14\xf5\xee"):
                raise ValueError("Unknown executable container")
            count = unpack(24, "<H")[0]
            if not 0 < count <= 4096:
                raise ValueError("Invalid SELF segment count")
            self_segments = [unpack(32 + i * 32, "<QQQQ") for i in range(count)]
            elf_offset = 32 + count * 32
        header = unpack(elf_offset, "<16sHHIQQQIHHHHHH")
        ident, phoff, phsize, phnum = header[0], header[5], header[9], header[10]
        if ident[:7] != b"\x7fELF\x02\x01\x01" or phsize != 56 or not 0 < phnum <= 512:
            raise ValueError("Unsupported ELF header")
        segments = []
        for index in range(phnum):
            kind, _, offset, rva, _, size, memory_size, _ = unpack(
                elf_offset + phoff + index * phsize, "<IIQQQQQQ")
            if kind != 1:
                continue
            if self_segments:
                matches = [row for row in self_segments
                           if row[0] & 0x800 and (row[0] >> 20) & 0xfff == index]
                if len(matches) != 1 or matches[0][2] != size or matches[0][3] != size:
                    raise ValueError("Compressed or ambiguous SELF PT_LOAD")
                offset = matches[0][1]
            if not size or memory_size < size or offset + size > total:
                raise ValueError("Invalid PT_LOAD payload range")
            segments.append((rva, offset, size))
        if not segments:
            raise ValueError("No PT_LOAD payload found")
        return segments


def mutate_unpatched_byte(source, target, patch_dir):
    excluded = []
    for _, _, filename in PROFILES:
        for mod in read_json(patch_dir / filename)["mods"]:
            for entry in mod["memory"]:
                start = int(entry["offset"], 16)
                excluded.append((start, start + len(bytes.fromhex(entry["off"]))))
    for rva, offset, size in load_segments(source):
        # Stay in the loaded payload, away from ELF/container metadata and patches.
        for delta in range(max(0x1000, size // 2), min(size, max(0x1000, size // 2) + 4096)):
            if not any(start <= rva + delta < end for start, end in excluded):
                shutil.copy2(source, target)
                with target.open("r+b") as stream:
                    stream.seek(offset + delta)
                    original = stream.read(1)
                    stream.seek(offset + delta)
                    stream.write(bytes([original[0] ^ 1]))
                return {"rva": hex(rva + delta), "file_offset": hex(offset + delta),
                        "outside_all_profile_patch_ranges": True}
    raise ValueError("Cannot find an unpatched PT_LOAD byte for the negative fixture")


def run_case(report, exe, game, name, args, expected_exit, markers):
    command = [str(exe), "--game", str(game), "--gt7-check-only", *args]
    case = {"name": name, "command": command, "cwd": str(exe.parent),
            "expected_exit": expected_exit, "expected_output": markers, "passed": False}
    report["cases"].append(case)
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=exe.parent, capture_output=True, timeout=60,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        case.update(exit_code=result.returncode, output=output,
                    passed=result.returncode == expected_exit
                    and all(marker in output for marker in markers)
                    and (expected_exit == 0 or VERIFIED not in output))
    except subprocess.TimeoutExpired as error:
        case.update(exit_code=None, error="Timed out after 60 seconds; subprocess was terminated",
                    output=((error.stdout or b"") + (error.stderr or b"")).decode("utf-8", errors="replace"))
    except OSError as error:
        case.update(exit_code=None, error=str(error))
    case["seconds"] = round(time.monotonic() - started, 3)
    print(f"{'PASS' if case['passed'] else 'FAIL'} {name}", flush=True)


def exercise(report, exe, reference, alternate, scratch):
    patch_dir = exe.parent / "patches" / "GT7"
    for label, game in (("reference", reference), ("alternate", alternate)):
        for name, args, filename in PROFILES:
            markers = [VERIFIED, filename]
            if label == "alternate":
                markers.append("accepted audited alternate executable image")
            run_case(report, exe, game, f"{label}-{name}", args, 0, markers)

        # The upstream wheel flags must coexist with both fork render modes.
        # Preflight must not initialize devices or read/create a wheel mapping.
        wheel_config = scratch / "wheel mapping with spaces.json"
        for name, args, filename in PROFILES:
            if name not in ("desktop-1440", "vr-100"):
                continue
            markers = [VERIFIED, filename]
            if label == "alternate":
                markers.append("accepted audited alternate executable image")
            run_case(report, exe, game, f"{label}-{name}-with-wheel",
                     [*args, "--wheel", "true", "--wheel-config", str(wheel_config)],
                     0, markers)
        if wheel_config.exists():
            raise AssertionError("Preflight created a wheel mapping")

    default_args = ["--gt7-desktop-height", "1440"]
    unknown = scratch / "unknown-eboot.bin"
    report["unknown_image_mutation"] = mutate_unpatched_byte(reference, unknown, patch_dir)
    run_case(report, exe, unknown, "unknown-loaded-image", default_args, 1,
             ["unsupported eboot.bin", "nothing was patched"])
    unknown.unlink()

    manifest_path = patch_dir / "patches.json"
    profile_path = patch_dir / "GT7-Desktop-Internal-1440.json"
    manifest_original, profile_original = manifest_path.read_bytes(), profile_path.read_bytes()

    def negative(name, change, message, rehash=False):
        try:
            change()
            if rehash:
                manifest = read_json(manifest_path)
                row = next(row for row in manifest["patches"] if row["file"] == profile_path.name)
                row["sha256"] = sha256(profile_path)
                write_json(manifest_path, manifest)
            run_case(report, exe, alternate, name, default_args, 1, [message])
        finally:
            manifest_path.write_bytes(manifest_original)
            profile_path.write_bytes(profile_original)

    def set_field(path, field, value):
        document = read_json(path)
        document[field] = value
        write_json(path, document)

    negative("manifest-identity", lambda: set_field(manifest_path, "executable_image_sha256", "0" * 64),
             "invalid GT7 patch manifest")
    negative("missing-profile", profile_path.unlink, "GT7 patch file missing or checksum mismatch")
    negative("profile-checksum", lambda: profile_path.write_bytes(profile_original + b"\n"),
             "GT7 patch file missing or checksum mismatch")
    identity_error = "GT7 patch identity or strict main-module requirements do not match"
    negative("profile-identity-rehashed", lambda: set_field(profile_path, "executable_image_sha256", "0" * 64),
             identity_error, rehash=True)
    negative("non-strict-profile-rehashed", lambda: set_field(profile_path, "strict_main_module", False),
             identity_error, rehash=True)

    def change_original_bytes():
        document = read_json(profile_path)
        entry = next(entry for mod in document["mods"] if mod.get("enabled", True) for entry in mod["memory"])
        original = bytearray.fromhex(entry["off"])
        original[0] ^= 1
        entry["off"] = original.hex().upper()
        write_json(profile_path, document)

    negative("original-instructions-rehashed", change_original_bytes,
             "GT7 patch original-byte or segment-range validation failed", rehash=True)
    conflicts = [
        ("desktop-with-vr", [*default_args, "--vr"], "desktop render profiles and VR cannot be combined"),
        ("scale-without-vr", ["--gt7-vr-scale", "100"], "--gt7-vr-scale requires --vr"),
        ("custom-patch-with-profile", [*default_args, "--game-patch", str(profile_path)],
         "GT7 profiles cannot be combined with --game-patch"),
        ("check-without-profile", [], "--gt7-check-only requires a GT7 profile"),
        ("invalid-desktop-height", ["--gt7-desktop-height", "1200"], "invalid or duplicate GT7 desktop height"),
        ("invalid-vr-scale", ["--vr", "--gt7-vr-scale", "101"], "invalid or duplicate GT7 VR scale"),
    ]
    for name, args, message in conflicts:
        run_case(report, exe, alternate, name, args, 1, [message])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--reference-eboot", type=Path, required=True)
    parser.add_argument("--alternate-eboot", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    emulator, reference, alternate = (path.resolve(strict=True) for path in
                                      (args.emulator, args.reference_eboot, args.alternate_eboot))
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "gt7-profile-preflight-report.json"
    patch_source = emulator.parent / "patches" / "GT7"
    dependencies = sorted(path for path in emulator.parent.iterdir() if path.is_file() and path.suffix.lower() == ".dll")
    inputs = {"reference": reference, "alternate": alternate}
    originals = [emulator, *dependencies, *sorted(path for path in patch_source.rglob("*") if path.is_file())]
    report = {"passed": False, "scope": "Packaged CLI preflight only; no game, GPU or SteamVR execution",
              "script_sha256": sha256(Path(__file__)), "cases": [], "inputs": {}, "runtime": {}}
    try:
        report["inputs"] = {label: {"path": str(path), "sha256_before": sha256(path)} for label, path in inputs.items()}
        report["runtime"] = {str(path): {"sha256_before": sha256(path)} for path in originals}
        with tempfile.TemporaryDirectory(prefix="gt7-preflight-", dir=output) as temp:
            scratch = Path(temp)
            runtime = scratch / "runtime"
            runtime.mkdir()
            for source in (emulator, *dependencies):
                shutil.copy2(source, runtime / source.name)
            shutil.copytree(patch_source, runtime / "patches" / "GT7")
            exercise(report, runtime / emulator.name, reference, alternate, scratch)
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        for label, details in report["inputs"].items():
            details["sha256_after"] = sha256(inputs[label])
            details["unchanged"] = details["sha256_before"] == details["sha256_after"]
        for path, details in report["runtime"].items():
            details["sha256_after"] = sha256(Path(path))
            details["unchanged"] = details["sha256_before"] == details["sha256_after"]
        report["passed"] = ("error" not in report and len(report["cases"]) == 33
                            and all(case["passed"] for case in report["cases"])
                            and all(item["unchanged"] for item in report["inputs"].values())
                            and all(item["unchanged"] for item in report["runtime"].values()))
        write_json(report_path, report)
    print(f"{'PASS' if report['passed'] else 'FAIL'}: {report_path}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
