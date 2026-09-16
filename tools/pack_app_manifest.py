import argparse
import struct
from pathlib import Path


APP_BASE = 0x08012000
APP_SIZE = 0x00020000
MANIFEST_RESERVED_SIZE = 64

APP_MANIFEST_ADDR = APP_BASE + APP_SIZE - MANIFEST_RESERVED_SIZE
MAX_IMAGE_SIZE = APP_SIZE - MANIFEST_RESERVED_SIZE

IMAGE_MANIFEST_MAGIC = 0x4D4E4653


def parse_uint32(value):
    try:
        result = int(value, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(
            "版本号必须是十进制或 0x 开头的十六进制数"
        )

    if result < 0 or result > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(
            "版本号超出 uint32 范围"
        )

    return result


def crc32(data):
    """
    与 Common/src/common_crc.c 中的
    common_crc32_calc() 保持一致。
    """
    crc = 0xFFFFFFFF

    for value in data:
        crc ^= value

        for _ in range(8):
            if (crc & 1) != 0:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1

    return crc ^ 0xFFFFFFFF


def build_manifest(image, image_version):
    image_size = len(image)

    if image_size == 0:
        raise ValueError("App 映像不能为空")

    if image_size > MAX_IMAGE_SIZE:
        raise ValueError(
            "App 映像超过最大长度: "
            f"0x{image_size:X} > 0x{MAX_IMAGE_SIZE:X}"
        )

    image_crc32 = crc32(image)

    first_16_bytes = struct.pack(
        "<IIII",
        IMAGE_MANIFEST_MAGIC,
        image_version,
        image_size,
        image_crc32,
    )

    manifest_crc32 = crc32(first_16_bytes)

    manifest = first_16_bytes + struct.pack(
        "<I",
        manifest_crc32,
    )

    if len(manifest) != 20:
        raise RuntimeError("manifest 长度错误")

    if crc32(manifest[:16]) != manifest_crc32:
        raise RuntimeError("manifest CRC 自校验失败")

    return manifest


def make_hex_record(address, record_type, data):
    if address < 0 or address > 0xFFFF:
        raise ValueError("Intel HEX 数据地址必须是 16 位")

    if len(data) > 0xFF:
        raise ValueError("Intel HEX 单条记录数据过长")

    record = bytes(
        [
            len(data),
            (address >> 8) & 0xFF,
            address & 0xFF,
            record_type & 0xFF,
        ]
    ) + data

    checksum = (-sum(record)) & 0xFF

    return ":" + (
        record + bytes([checksum])
    ).hex().upper()


def write_intel_hex(output_path, segments):
    lines = []
    current_upper_address = None

    for segment_address, segment_data in segments:
        offset = 0

        while offset < len(segment_data):
            absolute_address = segment_address + offset
            upper_address = absolute_address >> 16

            if upper_address != current_upper_address:
                lines.append(
                    make_hex_record(
                        0,
                        0x04,
                        upper_address.to_bytes(2, "big"),
                    )
                )
                current_upper_address = upper_address

            low_address = absolute_address & 0xFFFF

            chunk_size = min(
                16,
                len(segment_data) - offset,
                0x10000 - low_address,
            )

            chunk = segment_data[
                offset:offset + chunk_size
            ]

            lines.append(
                make_hex_record(
                    low_address,
                    0x00,
                    chunk,
                )
            )

            offset += chunk_size

    lines.append(":00000001FF")

    output_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with output_path.open(
        "w",
        encoding="ascii",
        newline="\n",
    ) as file:
        file.write("\n".join(lines))
        file.write("\n")


def main():
    parser = argparse.ArgumentParser(
        description="根据 App.bin 自动生成 manifest 和合并 HEX"
    )

    parser.add_argument(
        "--image",
        required=True,
        type=Path,
        help="App 原始 bin 文件",
    )

    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="包含 App 和 manifest 的 HEX 文件",
    )

    parser.add_argument(
        "--version",
        required=True,
        type=parse_uint32,
        help="App 映像版本号，例如 0x00000001",
    )

    args = parser.parse_args()

    if not args.image.is_file():
        raise SystemExit(
            f"找不到 App 映像: {args.image}"
        )

    image = args.image.read_bytes()

    image_end = APP_BASE + len(image)

    if image_end > APP_MANIFEST_ADDR:
        raise SystemExit(
            "App 映像进入 manifest 保留区，不能继续生成"
        )

    try:
        manifest = build_manifest(
            image,
            args.version,
        )
    except ValueError as error:
        raise SystemExit(str(error))

    manifest_path = args.output.with_name(
        args.output.stem + ".manifest.bin"
    )

    manifest_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    manifest_path.write_bytes(manifest)

    write_intel_hex(
        args.output,
        [
            (APP_BASE, image),
            (APP_MANIFEST_ADDR, manifest),
        ],
    )

    image_crc32 = struct.unpack_from(
        "<I",
        manifest,
        12,
    )[0]

    manifest_crc32 = struct.unpack_from(
        "<I",
        manifest,
        16,
    )[0]

    print(f"image_size       = 0x{len(image):08X}")
    print(
        "image_range      = "
        f"0x{APP_BASE:08X} - "
        f"0x{image_end - 1:08X}"
    )
    print(f"image_crc32      = 0x{image_crc32:08X}")
    print(f"manifest_address = 0x{APP_MANIFEST_ADDR:08X}")
    print(f"manifest_crc32   = 0x{manifest_crc32:08X}")
    print(
        "manifest_bytes   = "
        + manifest.hex(" ").upper()
    )
    print(f"manifest_bin     = {manifest_path}")
    print(f"merged_hex       = {args.output}")


if __name__ == "__main__":
    main()
