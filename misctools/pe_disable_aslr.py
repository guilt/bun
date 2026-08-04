#!/usr/bin/env python3
"""Disable ASLR on a 32-bit (i586) PE executable in place.

Purpose: the win9x build (bun-debug.exe) is linked with a randomized base
(DYNAMIC_BASE / HighEntropyVA), but Windows 9x/2000/XP only load PE images at
the ImageBase recorded in the header, and the C Loop interpreter benefits from
a stable base. Clear the ASLR characteristics and fix the 32-bit image base to
0x400000.

Works on both PE32 (32-bit) and PE32+ (64-bit) images. For the win9x target it
is expected to be a PE32 image (only PE32 gets its base rewritten).

Usage:
    python pe_disable_aslr.py <path-to-exe>

The file is modified in place.
"""

import os
import struct
import sys

IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040
IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020
ASLR_FLAGS = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA

WANTED_BASE = 0x400000


def patch(path: str) -> None:
    with open(path, "rb") as f:
        buf = bytearray(f.read())

    if buf[0:2] != b"MZ":
        raise ValueError("not a PE file (missing MZ magic)")

    (pe_off,) = struct.unpack_from("<I", buf, 0x3C)
    if buf[pe_off:pe_off + 4] != b"PE\0\0":
        raise ValueError("not a PE file (missing PE signature)")

    opt_off = pe_off + 4 + 20  # signature + COFF file header
    (magic,) = struct.unpack_from("<H", buf, opt_off)

    if magic == 0x10B:
        # PE32: ImageBase +0x1C (u32), DllCharacteristics +0x46 (u16)
        base_fmt, base_off, dll_char_off = "<I", 0x1C, 0x46
    elif magic == 0x20B:
        # PE32+: ImageBase +0x18 (u64), DllCharacteristics +0x46 (u16)
        base_fmt, base_off, dll_char_off = "<Q", 0x18, 0x46
    else:
        raise ValueError(f"unknown optional-header magic 0x{magic:04X}")

    old_char = struct.unpack_from("<H", buf, opt_off + dll_char_off)[0]
    new_char = old_char & ~ASLR_FLAGS
    struct.pack_into("<H", buf, opt_off + dll_char_off, new_char)

    old_base = struct.unpack_from(base_fmt, buf, opt_off + base_off)[0]
    if magic == 0x10B:
        struct.pack_into(base_fmt, buf, opt_off + base_off, WANTED_BASE)
        new_base = hex(WANTED_BASE)
    else:
        new_base = hex(old_base)

    with open(path, "wb") as f:
        f.write(buf)

    print(
        f"patched {path}\n"
        f"  DllCharacteristics 0x{old_char:04X} -> 0x{new_char:04X} "
        f"(cleared DYNAMIC_BASE, HIGH_ENTROPY_VA)\n"
        f"  ImageBase          0x{old_base:X} -> {new_base}"
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: python {sys.argv[0]} <pe-file>", file=sys.stderr)
        return 2
    target = sys.argv[1]
    if not os.path.isfile(target):
        print(f"error: {target!r} is not a file", file=sys.stderr)
        return 2
    try:
        patch(target)
    except Exception as e:  # noqa: BLE001
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())