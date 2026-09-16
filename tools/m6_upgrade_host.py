#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""通过 USB-RS485 发送 M6 在线升级流程。"""

import argparse
import struct
import time
from pathlib import Path

import serial


FRAME_HEADER = 0xA5B6
FRAME_TAIL = 0xB6A5
FRAME_VERSION = 0x02
FRAME_TYPE_COMMAND = 0x01
FRAME_TYPE_RESPONSE = 0x02
FRAME_TYPE_ERROR = 0xFF

CMD_REBOOT = 0x0101
CMD_QUERY_VERSION = 0x0102
CMD_ENTER_BOOT = 0x0500
CMD_BEGIN = 0x0501
CMD_DATA = 0x0502
CMD_END = 0x0503
CMD_INSTALL = 0x0504
CMD_ABORT = 0x0505

APP_BASE = 0x08012000
FIRMWARE_HEADER_MAGIC = 0x5AA5C33C
FIRMWARE_HEADER_PACKAGE_VERSION = 1
FIRMWARE_HEADER_SIZE = 32
MAX_IMAGE_SIZE = 0x00020000 - 64


def crc16_modbus(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def crc32(data):
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def build_frame(address, command, sequence, payload=b""):
    body = struct.pack(
        ">HBHBHHH",
        FRAME_HEADER,
        FRAME_VERSION,
        address,
        FRAME_TYPE_COMMAND,
        command,
        sequence,
        len(payload),
    ) + payload
    return body + struct.pack(">H", crc16_modbus(body)) + struct.pack(">H", FRAME_TAIL)


def build_firmware_header(image, version, flags=0):
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
    header = struct.pack(
        "<IHHIIIIII",
        FIRMWARE_HEADER_MAGIC,
        FIRMWARE_HEADER_PACKAGE_VERSION,
        FIRMWARE_HEADER_SIZE,
        version,
        len(image),
        APP_BASE,
        image_crc32,
        flags,
        header_crc32,
    )
    return header, image_crc32, header_crc32


class UpgradeHost:
    def __init__(self, port, address, timeout):
        self.address = address
        self.ser = serial.Serial(
            port=port,
            baudrate=115200,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
        )
        self.timeout = timeout

    def close(self):
        self.ser.close()

    def _read_exact(self, length, deadline):
        data = bytearray()
        while len(data) < length and time.monotonic() < deadline:
            chunk = self.ser.read(length - len(data))
            if chunk:
                data.extend(chunk)
        if len(data) != length:
            raise TimeoutError("等待响应超时")
        return bytes(data)

    def _read_frame(self, deadline):
        first = None
        while time.monotonic() < deadline:
            value = self.ser.read(1)
            if not value:
                continue
            if value[0] != 0xA5:
                continue
            second = self.ser.read(1)
            if second and second[0] == 0xB6:
                first = b"\xA5\xB6"
                break

        if first is None:
            raise TimeoutError("等待响应帧头超时")

        header = first + self._read_exact(10, deadline)
        payload_length = struct.unpack(">H", header[10:12])[0]
        if payload_length > 1024:
            raise RuntimeError("响应数据长度超过协议上限")

        tail = self._read_exact(payload_length + 4, deadline)
        frame = header + tail

        if frame[-2:] != b"\xB6\xA5":
            raise RuntimeError("响应帧尾错误")
        if crc16_modbus(frame[:-4]) != struct.unpack(">H", frame[-4:-2])[0]:
            raise RuntimeError("响应 CRC 错误")

        return {
            "type": frame[5],
            "command": struct.unpack(">H", frame[6:8])[0],
            "sequence": struct.unpack(">H", frame[8:10])[0],
            "payload": frame[12:12 + payload_length],
        }

    def transact(self, command, sequence, payload=b"", timeout=None):
        wait_timeout = self.timeout if timeout is None else timeout
        frame = build_frame(self.address, command, sequence, payload)
        self.ser.reset_input_buffer()
        self.ser.write(frame)
        self.ser.flush()

        deadline = time.monotonic() + wait_timeout
        while time.monotonic() < deadline:
            response = self._read_frame(deadline)
            if ((response["type"] in (FRAME_TYPE_RESPONSE, FRAME_TYPE_ERROR)) and
                    response["command"] == command and
                    response["sequence"] == sequence):
                return response
        raise TimeoutError("等待匹配响应超时")

    @staticmethod
    def require_ok(response, name, expected_sequence=None):
        payload = response["payload"]
        if response["type"] == FRAME_TYPE_ERROR:
            code = payload[0] if payload else 0xFF
            raise RuntimeError("%s 被 Boot 拒绝，错误码 0x%02X" % (name, code))
        if response["type"] != FRAME_TYPE_RESPONSE or not payload or payload[0] != 0xFF:
            raise RuntimeError("%s 返回格式错误：%s" % (name, payload.hex(" ")))
        if expected_sequence is not None:
            if len(payload) != 3 or payload[1:] != struct.pack(">H", expected_sequence):
                raise RuntimeError("%s ACK 序号错误：%s" % (name, payload.hex(" ")))


def parse_args():
    parser = argparse.ArgumentParser(description="M6 BEGIN/DATA/END/INSTALL 主机测试")
    parser.add_argument("--port", default="COM14")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0x0001)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--version", type=lambda value: int(value, 0), default=2)
    parser.add_argument("--no-reset-first", action="store_true")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--final-wait", type=float, default=45.0)
    return parser.parse_args()


def main():
    args = parse_args()
    image = args.image.read_bytes()

    if len(image) == 0 or len(image) > MAX_IMAGE_SIZE:
        raise SystemExit("App.bin 长度非法：0x%X" % len(image))
    if len(image) < 8:
        raise SystemExit("App.bin 小于向量表长度")

    msp, reset_handler = struct.unpack_from("<II", image, 0)
    header, image_crc32, header_crc32 = build_firmware_header(image, args.version)

    print("image        = %s" % args.image)
    print("image_size   = 0x%08X" % len(image))
    print("image_crc32  = 0x%08X" % image_crc32)
    print("header_crc32 = 0x%08X" % header_crc32)
    print("MSP          = 0x%08X" % msp)
    print("ResetHandler = 0x%08X" % reset_handler)
    print("header       = %s" % header.hex(" ").upper())

    host = UpgradeHost(args.port, args.address, args.timeout)
    try:
        if not args.no_reset_first:
            print("[1/5] 发送正式 ENTER_BOOT，请求当前 App 进入 Boot")
            response = host.transact(CMD_ENTER_BOOT, 0x0100, timeout=3.0)
            UpgradeHost.require_ok(response, "App ENTER_BOOT")
            time.sleep(1.5)

        print("[1/5] BEGIN")
        response = host.transact(CMD_BEGIN, 0x0001, header, timeout=8.0)
        UpgradeHost.require_ok(response, "BEGIN")

        chunk_size = 256
        chunk_count = (len(image) + chunk_size - 1) // chunk_size
        print("[2/5] DATA：%d 片" % chunk_count)
        for sequence, offset in enumerate(range(0, len(image), chunk_size)):
            chunk = image[offset:offset + chunk_size]
            payload = (
                struct.pack(">HIH", sequence, offset, len(chunk)) +
                chunk +
                struct.pack(">H", crc16_modbus(chunk))
            )
            response = host.transact(CMD_DATA, sequence, payload, timeout=5.0)
            UpgradeHost.require_ok(response, "DATA[%d]" % sequence, sequence)
            if ((sequence + 1) % 32 == 0) or sequence == chunk_count - 1:
                print("  DATA %d/%d" % (sequence + 1, chunk_count))

        print("[3/5] END")
        response = host.transact(CMD_END, chunk_count + 1, timeout=12.0)
        UpgradeHost.require_ok(response, "END")

        print("[4/5] INSTALL")
        response = host.transact(CMD_INSTALL, chunk_count + 2, timeout=8.0)
        UpgradeHost.require_ok(response, "INSTALL")
        print("  INSTALL ACK 已收到，Boot 开始执行安装状态机")

        print("[5/5] 等待新 App 完成 CONFIRMED 并回到 IDLE")
        deadline = time.monotonic() + args.final_wait
        poll_sequence = 0x7000
        while time.monotonic() < deadline:
            time.sleep(1.0)
            try:
                response = host.transact(
                    CMD_QUERY_VERSION,
                    poll_sequence,
                    timeout=1.0,
                )
                poll_sequence = (poll_sequence + 1) & 0xFFFF
                if response["type"] == FRAME_TYPE_RESPONSE and response["payload"]:
                    payload = response["payload"]
                    if payload[0] == 0xFF and len(payload) >= 5:
                        version = payload[1:5]
                        print("  App 版本应答：%s" % version.hex(" "))
                        if version == bytes((0x00, 0x01, 0x00, 0x02)):
                            print("M6-0E ONLINE UPGRADE PASS")
                            return 0
                elif response["type"] == FRAME_TYPE_ERROR:
                    continue
            except (TimeoutError, RuntimeError):
                continue

        raise RuntimeError("安装后在 %.1f 秒内未读到 App v2 应答" % args.final_wait)
    finally:
        host.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (serial.SerialException, TimeoutError, RuntimeError) as error:
        print("M6-0E ONLINE UPGRADE FAIL: %s" % error)
        raise SystemExit(1)
