#!/usr/bin/env python3
"""Fail if the fbt and ufbt builds ship different app metadata.

This repo declares the same app twice: once in the root `application.fam` (used
by ufbt) and once in the `App()` block that `make copy-plugin` appends to the
firmware's nfc manifest (used by fbt). Nothing else compares them, so a
one-sided edit -- a `fap_version` bump, say -- would ship divergent metadata
with CI green.

`.fapmeta` is that metadata as it actually reaches the device, so compare it
rather than the manifest text. Its layout is fixed by the SDK's
`scripts/fbt/elfmanifest.py`.

`api_version` is deliberately excluded: fbt builds against the pinned firmware
submodule and ufbt against the floating `release` SDK channel, so those two
legitimately diverge whenever upstream publishes a new API version. It is
reported for information only.
"""

import struct
import sys

_SECTION = b".fapmeta"
_MAGIC = b"HDGR"

# Fields that come from the app manifest. Divergence here is the bug this
# script exists to catch.
_MANIFEST_FIELDS = ("hardware_target_id", "stack_size", "fap_version", "name", "icon")


def read_section(path: str, name: bytes) -> bytes:
    """Return the contents of `name` from a little-endian ELF32 file."""
    with open(path, "rb") as handle:
        blob = handle.read()

    if blob[:6] != b"\x7fELF\x01\x01":
        raise SystemExit(f"{path}: not a little-endian 32-bit ELF")

    (e_shoff,) = struct.unpack_from("<I", blob, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", blob, 0x2E)

    def header(index: int) -> tuple[int, ...]:
        return struct.unpack_from("<10I", blob, e_shoff + index * e_shentsize)

    strtab = header(e_shstrndx)[4]
    for index in range(e_shnum):
        sh_name, _, _, _, sh_offset, sh_size = header(index)[:6]
        start = strtab + sh_name
        if blob[start : blob.index(b"\0", start)] == name:
            return blob[sh_offset : sh_offset + sh_size]

    raise SystemExit(f"{path}: no {name.decode()} section")


def parse_fapmeta(blob: bytes) -> dict[str, object]:
    magic, manifest_version, api_version, hardware_target_id = struct.unpack_from(
        "<4sIIh", blob, 0
    )
    if magic != _MAGIC:
        raise SystemExit(f"unexpected .fapmeta magic {magic!r}")

    stack_size, app_version, name, has_icon, icon = struct.unpack_from(
        "<hI32s?32s", blob, 14
    )
    return {
        "manifest_version": manifest_version,
        "api_version": f"{api_version >> 16}.{api_version & 0xFFFF}",
        "hardware_target_id": hardware_target_id,
        "stack_size": stack_size,
        "fap_version": f"{app_version >> 16}.{app_version & 0xFFFF}",
        "name": name.rstrip(b"\0").decode("ascii"),
        "icon": icon.hex() if has_icon else None,
    }


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit("usage: compare_fapmeta.py <fbt.fal> <ufbt.fal>")

    fbt_path, ufbt_path = argv[1], argv[2]
    fbt = parse_fapmeta(read_section(fbt_path, _SECTION))
    ufbt = parse_fapmeta(read_section(ufbt_path, _SECTION))

    print(f"{'field':<20} {'fbt':<24} ufbt")
    for field in ("manifest_version", "api_version", *_MANIFEST_FIELDS):
        marker = "" if fbt[field] == ufbt[field] else "  <-- differs"
        print(f"{field:<20} {str(fbt[field]):<24} {ufbt[field]}{marker}")

    mismatched = [f for f in _MANIFEST_FIELDS if fbt[f] != ufbt[f]]
    if mismatched:
        print(flush=True)
        print(
            f"ERROR: the fbt and ufbt builds disagree on {', '.join(mismatched)}.\n"
            "The root application.fam and the App() block in the Makefile's\n"
            "copy-plugin target have diverged. Update both.",
            file=sys.stderr,
        )
        return 1

    if fbt["api_version"] != ufbt["api_version"]:
        print()
        print(
            "Note: api_version differs. That is expected drift between the pinned\n"
            "firmware submodule and the floating ufbt SDK channel, not a manifest\n"
            "problem -- but it does mean plugin/nfc_supported_card_plugin.h is due\n"
            "for a re-diff against upstream."
        )

    print()
    print("Manifest metadata agrees across both build paths.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
