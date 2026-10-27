#!/usr/bin/env python3

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


PLACEHOLDER = re.compile(r"%(?:\d+|n)")


def main() -> int:
    catalog = Path(sys.argv[1])
    root = ET.parse(catalog).getroot()
    failures: list[str] = []
    messages = root.findall("./context/message")

    for context in root.findall("./context"):
        label = context.findtext("name", default="unknown")
        for message in context.findall("message"):
            source = message.findtext("source", default="")
            translation = message.find("translation")
            if translation is None or translation.get("type") == "unfinished":
                failures.append(f"{label}: unfinished translation for {source!r}")
                continue
            translated = "".join(translation.itertext()).strip()
            if not translated:
                failures.append(f"{label}: empty translation for {source!r}")
                continue
            if sorted(PLACEHOLDER.findall(source)) != sorted(PLACEHOLDER.findall(translated)):
                failures.append(f"{label}: placeholder mismatch for {source!r}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Translation catalog passed: {len(messages)} complete messages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
