"""Verify the portable Windows release ZIP without launching either executable."""

import argparse
import hashlib
import pathlib
import re
import zipfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as release:
        names = release.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate ZIP members")
        if release.testzip() is not None:
            raise ValueError("ZIP CRC verification failed")
        roots = {name.split("/")[0] for name in names}
        if len(roots) != 1:
            raise ValueError("release must have one top-level directory")
        root = roots.pop() + "/"
        if not re.fullmatch(r"Tohi-GT7-\d+\.\d+\.\d+-Windows-x64/", root):
            raise ValueError("unexpected release folder name")
        for name in names:
            path = pathlib.PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                raise ValueError(f"unsafe ZIP member: {name}")
        sums_name = root + "SHA256SUMS.txt"
        records = release.read(sums_name).decode("utf-8").splitlines()
        expected = {}
        for record in records:
            digest, relative = record.split("  ", 1)
            if relative in expected or relative.startswith("/") or ".." in relative.split("/"):
                raise ValueError("invalid checksum manifest entry")
            expected[relative] = digest
        actual = {name[len(root):] for name in names if name.startswith(root)
                  and not name.endswith("/") and name != sums_name}
        if set(expected) != actual:
            raise ValueError("checksum manifest does not cover exactly the bundled files")
        for relative, digest in expected.items():
            if hashlib.sha256(release.read(root + relative)).hexdigest() != digest:
                raise ValueError(f"SHA-256 mismatch: {relative}")
        required = {"launcher.exe", "kyty_emulator.exe", "Qt6Core.dll", "Qt6Gui.dll",
                    "Qt6Network.dll", "Qt6Svg.dll", "Qt6Widgets.dll", "msvcp140.dll",
                    "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140_1.dll", "msvcp140_2.dll",
                    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "icuuc.dll",
                    "libwinpthread-1.dll", "platforms/qwindows.dll",
                    "kyty_dlss_backend.dll", "nvngx_dlss.dll", "amd_fidelityfx_vk.dll",
                    "patches/GT7/patches.json", "LICENSE", "licenses/Kyty-MIT.txt",
                    "licenses/qt/Qt-NOTICE.md", "licenses/qt/Qt-LGPL-3.0-only.txt",
                    "licenses/qt/Qt-GPL-3.0-only.txt", "licenses/openvr/LICENSE",
                    "licenses/winpthread/COPYING", "licenses/opus/COPYING",
                    "licenses/ffmpeg/copyright", "licenses/ffmpeg/SOURCE.txt",
                    "licenses/nvidia/LICENSE.txt", "licenses/nvidia/NVIDIA-DLSS-NOTICE.md",
                    "licenses/fidelityfx/LICENSE.txt", "licenses/fidelityfx/provenance.json",
                    "docs/GT7.md", "docs/GT7-FSR-FRAME-GENERATION.md",
                    "translations/qt_zh_CN.qm"}
        if not required.issubset(actual):
            raise ValueError(f"required runtime or notice files are missing: {sorted(required - actual)}")
        vendor_hashes = {
            "nvngx_dlss.dll": "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e",
            "amd_fidelityfx_vk.dll": "a1624cc4238fef046f30c4d80ce3f47be63fc5f5373f49e3ee9edb9960f54c78",
        }
        for name, digest in vendor_hashes.items():
            if expected[name].lower() != digest:
                raise ValueError(f"unvalidated vendor runtime: {name}")
        forbidden_suffixes = {".pdb", ".map", ".log", ".dmp", ".gpudmp", ".bin", ".raw", ".rgba16f", ".ini"}
        forbidden_parts = {"_savedata", "_downloaddata", "_trophies", "cache", "caches",
                           "shader-cache", "shader_cache", "_pipelinecache", "capture",
                           "captures", "log", "logs", "outputs", "scripts"}
        for name in actual:
            path = pathlib.PurePosixPath(name)
            lowered = {part.lower() for part in path.parts}
            if path.name.lower() in {"eboot.bin", "gt7save.bin"} or lowered & forbidden_parts:
                raise ValueError(f"game, save or cache content found in release: {name}")
            if path.suffix.lower() in forbidden_suffixes:
                raise ValueError(f"development symbol, dump or log found in release: {name}")

        # Public metadata and docs must not disclose this builder's absolute workspace path.
        text_extensions = {".md", ".txt", ".json", ".ps1", ".xml", ".ini"}
        windows_absolute_path = re.compile(rb"[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/]", re.I)
        for name in actual:
            path = pathlib.PurePosixPath(name)
            if path.suffix.lower() in text_extensions:
                if windows_absolute_path.search(release.read(root + name)):
                    raise ValueError(f"local absolute path found in release text: {name}")
        print(f"PASS: {len(actual)} files, ZIP CRC and SHA-256, pinned DLSS/FSR runtimes; "
              "no game/save/cache/log/symbol/legacy-script content")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
