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
        required = {"launcher.exe", "kyty_emulator.exe", "Qt6Core.dll", "msvcp140.dll",
                    "vcruntime140.dll", "libwinpthread-1.dll", "platforms/qwindows.dll",
                    "patches/GT7/patches.json", "licenses/qt/Qt-LGPL-3.0-only.txt",
                    "scripts/GT7-Start.ps1", "docs/GT7-VR.md",
                    "translations/qt_zh_CN.qm"}
        if not required.issubset(actual):
            raise ValueError("required runtime or notice files are missing")
        forbidden_suffixes = {".pdb", ".map", ".log", ".dmp"}
        forbidden_parts = {"_savedata", "cache", "caches", "shader-cache", "shader_cache"}
        for name in actual:
            path = pathlib.PurePosixPath(name)
            lowered = {part.lower() for part in path.parts}
            if path.name.lower() == "eboot.bin" or lowered & forbidden_parts:
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
        print(f"PASS: {len(actual)} files, ZIP CRC and SHA-256; no game/save/cache/log/symbol content")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
