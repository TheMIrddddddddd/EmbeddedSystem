#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 TF 离线升级包：firmware_header_t(32B) + 原始 App.bin。"""

import argparse
import struct
from pathlib import Path


APP_BASE = 0x08012000
FIRMWARE_HEADER_MAGIC = 0x5AA5C33C
FIRMWARE_HEADER_PACKAGE_VERSION = 1
FIRMWARE_HEADER_SIZE = 32
MAX_IMAGE_SIZE = 0x00020000 - 64
FLAG_ALLOW_DOWNGRADE = 1 << 0
FLAG_FORCE_UPGRADE = 1 << 1


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xEDB88320) if (crc & 1) else (crc >> 1)
    return crc ^ 0xFFFFFFFF


def build_header(image: bytes, version: int, flags: int) -> tuple[bytes, int, int]:
    image_crc32 = crc32(image)
    header_without_crc = struct.pack(
        "<IHHIIIIII",
        FIRMWARE_HEADER_MAGIC,
        FIRMWARE_HEADER_PACKAGE_VERSION,
        FIRMWARE_HEADER_SIZE,
        version,
        len(image),
        APP_BASE,
        image_crc32,
        flags,
        0,
    )
    header_crc32 = crc32(header_without_crc[:28])
    header = header_without_crc[:28] + struct.pack("<I", header_crc32)
    return header, image_crc32, header_crc32


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="生成 /firmware/app.bin 的 TF 离线升级包"
    )
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--allow-downgrade", action="store_true")
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = args.image.read_bytes()
    if not image or len(image) > MAX_IMAGE_SIZE:
        raise SystemExit("App.bin 长度非法：0x%X" % len(image))

    flags = 0
    if args.allow_downgrade:
        flags |= FLAG_ALLOW_DOWNGRADE
    if args.force:
        flags |= FLAG_FORCE_UPGRADE

    header, image_crc32, header_crc32 = build_header(
        image, args.version, flags
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + image)

    print("image        = %s" % args.image)
    print("output       = %s" % args.output)
    print("package_size = 0x%X" % (len(header) + len(image)))
    print("image_size   = 0x%X" % len(image))
    print("image_crc32  = 0x%08X" % image_crc32)
    print("header_crc32 = 0x%08X" % header_crc32)
    print("header       = %s" % header.hex(" ").upper())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
