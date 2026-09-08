#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rs485_host.py - M4-4 板测主机端工具（USB-RS485 适配器用）

帧格式严格对齐《01》七-2 与固件 protocol_frame.c：
  A5B6 | 02 | 地址(2B大端) | 类型(1B) | 命令(2B大端) | 序列(2B大端) |
  长度(2B大端) | 数据 | CRC16-Modbus(大端,覆盖帧头至数据末) | B6A5

用法:
  pip install pyserial
  python rs485_host.py COM9 all        # 跑全部用例（M4-4c 全量 18 例）
  python rs485_host.py COM9 one 0201   # 单发一条查询（命令字十六进制）
"""

import struct
import sys
import time

import serial

HEADER = 0xA5B6
TAIL = 0xB6A5
VERSION = 0x02
TYPE_COMMAND = 0x01
TYPE_RESPONSE = 0x02
TYPE_ERROR = 0xFF

ERROR_NAMES = {
    0x01: "CRC错误", 0x02: "长度错误", 0x03: "非法命令字",
    0x04: "非法参数值", 0x05: "设备忙",
}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_frame(address, frame_type, command, sequence, payload=b"") -> bytes:
    body = struct.pack(">HBHBHHH", HEADER, VERSION, address,
                       frame_type, command, sequence, len(payload)) + payload
    return body + struct.pack(">H", crc16_modbus(body)) + struct.pack(">H", TAIL)


def break_crc(frame: bytes) -> bytes:
    """翻转 CRC 第一个字节，制造 K-01 类坏帧（帧形完整）"""
    broken = bytearray(frame)
    broken[-4] ^= 0xFF
    return bytes(broken)


class Rs485Host:
    def __init__(self, port: str, baud: int = 115200):
        self.ser = serial.Serial(port, baud, timeout=2.0)

    def transact(self, address, command, sequence, payload=b"",
                 expect_reply=True, raw_frame=None, timeout=2.0):
        """发送并收应答。返回应答帧 bytes，或 None（超时无应答）。
        自动上报开启后，0x0382 事件帧可能插在应答前面，逐帧跳过。"""
        frame = raw_frame if raw_frame is not None else \
            build_frame(address, TYPE_COMMAND, command, sequence, payload)

        self.ser.reset_input_buffer()
        self.ser.write(frame)

        if not expect_reply:
            time.sleep(0.3)
            leftover = self.ser.read(64)
            return None if not leftover else leftover

        self.ser.timeout = timeout
        deadline = time.time() + timeout

        while time.time() < deadline:
            head = self._read_exact(12)
            if head is None:
                return None
            payload_length = struct.unpack(">H", head[10:12])[0]
            if payload_length > 1024:
                return None
            rest = self._read_exact(payload_length + 4)   # 数据 + CRC + 帧尾
            if rest is None:
                return None
            frame_rx = head + rest

            frame_type = head[5]
            frame_cmd = struct.unpack(">H", head[6:8])[0]
            frame_seq = struct.unpack(">H", head[8:10])[0]

            # 只取与本请求匹配的应答帧；设备主动事件帧（05）跳过继续等
            if frame_type in (TYPE_RESPONSE, TYPE_ERROR) and \
               frame_cmd == command and frame_seq == sequence:
                return frame_rx

        return None

    def _read_exact(self, count: int):
        data = b""
        deadline = time.time() + self.ser.timeout
        while len(data) < count and time.time() < deadline:
            data += self.ser.read(count - len(data))
        return data if len(data) == count else None

    def drain_events(self, seconds: float):
        """收 seconds 秒原始帧，返回其中的事件帧列表 [(命令字, 载荷), ...]"""
        events = []
        deadline = time.time() + seconds
        while time.time() < deadline:
            head = self.ser.read(12)
            if len(head) < 12:
                continue
            length = struct.unpack(">H", head[10:12])[0]
            if length > 1024:
                continue
            rest = b""
            while len(rest) < length + 4:
                chunk = self.ser.read(length + 4 - len(rest))
                if not chunk:
                    break
                rest += chunk
            if head[5] == 0x05:
                events.append((struct.unpack(">H", head[6:8])[0], rest[:length]))
        return events

    def verify_response(self, resp: bytes, address, command, sequence):
        """校验应答帧结构，返回 (ok, 描述, 数据区)"""
        if resp is None:
            return False, "超时无应答", b""
        if len(resp) < 16:
            return False, "应答过短", b""
        if struct.unpack(">H", resp[0:2])[0] != HEADER:
            return False, "应答帧头错", b""
        if resp[2] != VERSION:
            return False, "应答版本错", b""
        if struct.unpack(">H", resp[3:5])[0] != address:
            return False, "应答地址错", b""
        if struct.unpack(">H", resp[6:8])[0] != command:
            return False, "应答命令字错", b""
        if struct.unpack(">H", resp[8:10])[0] != sequence:
            return False, "应答序列号错", b""
        if struct.unpack(">H", resp[-2:])[0] != TAIL:
            return False, "应答帧尾错", b""
        payload = resp[12:-4]
        if crc16_modbus(resp[:-4]) != struct.unpack(">H", resp[-4:-2])[0]:
            return False, "应答CRC错", b""
        if resp[5] == TYPE_RESPONSE:
            if payload[0] != 0xFF:
                return False, "OK应答首字节不是FF", payload
            return True, "OK", payload[1:]
        if resp[5] == TYPE_ERROR:
            code = payload[0]
            return True, "ERROR:%02X(%s)" % (code,
                                             ERROR_NAMES.get(code, "未知")), b""
        return False, "应答类型错: %02X" % resp[5], b""


def run_all(host: Rs485Host):
    results = []

    def check(name, cond, detail=""):
        results.append(cond)
        print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, detail))

    def query(command, seq, payload=b""):
        r = host.transact(0x0001, command, seq, payload)
        return host.verify_response(r, 0x0001, command, seq)

    def cmd_ok(command, seq, payload=b""):
        ok, msg, _ = query(command, seq, payload)
        return ok and msg == "OK"

    print("=== M4-4c RS485 板测 ===")

    # ---- 4b 回归 ----
    r = host.transact(0x0001, 0x0103, 1)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0103, 1)
    check("0x0103 查询设备ID", ok and data.hex() == "0001", "id=%s" % data.hex())

    r = host.transact(0x0001, 0x0201, 2)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0201, 2)
    ch0 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("0x0201 查询CH0", ok and 0.0 <= ch0 <= 3.5, "%.3fV" % ch0)

    # ---- 4c：系统管理 ----
    r = host.transact(0x0001, 0x0102, 10)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0102, 10)
    check("0x0102 查询固件版本", ok and len(data) == 4,
          "v%d.%d.%d.%d" % tuple(data) if len(data) == 4 else msg)

    r = host.transact(0x0001, 0x0105, 11)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0105, 11)
    baud = struct.unpack(">I", data[:4])[0] if len(data) >= 4 else 0
    check("0x0105 查询波特率", ok and baud == 115200, "%d" % baud)

    # 写 ID：本机旧地址发送；此后设备在新地址 0x0008 应答
    r = host.transact(0x0001, 0x0104, 12, struct.pack(">H", 0x0008))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0104, 12)
    r = host.transact(0x0008, 0x0103, 13)
    ok2, _, data = host.verify_response(r, 0x0008, 0x0103, 13)
    check("0x0104 写设备ID→新地址回读", ok and ok2 and data.hex() == "0008",
          "id=%s" % data.hex())

    # 长度错（在当前地址 0x0008 下发）
    r = host.transact(0x0008, 0x0104, 14, b"\x00")
    ok, msg, _ = host.verify_response(r, 0x0008, 0x0104, 14)
    check("0x0104 长度错→0x02", ok and msg == "ERROR:02(长度错误)", msg)

    # 写回默认 0001（从 0x0008 地址发），回老地址确认
    r = host.transact(0x0008, 0x0104, 15, struct.pack(">H", 0x0001))
    ok, msg, _ = host.verify_response(r, 0x0008, 0x0104, 15)
    r = host.transact(0x0001, 0x0103, 16)
    ok2, _, data = host.verify_response(r, 0x0001, 0x0103, 16)
    check("0x0104 写回默认ID", ok and ok2 and data.hex() == "0001",
          "id=%s" % data.hex())

    # ---- 4c：DAC ----
    r = host.transact(0x0001, 0x0301, 20, struct.pack(">H", 0x0200))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0301, 20)
    time.sleep(0.4)   # 等 3 点滤波窗口收敛（300ms）再读
    r = host.transact(0x0001, 0x0202, 21)
    _, _, data = host.verify_response(r, 0x0001, 0x0202, 21)
    ch1 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("0x0301 DAC=512→CH1≈0.41V", ok and 0.35 <= ch1 <= 0.48,
          "%.3fV" % ch1)

    r = host.transact(0x0001, 0x0301, 22, struct.pack(">H", 0x1000))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0301, 22)
    check("0x0301 DAC=4096→0x04", ok and msg == "ERROR:04(非法参数值)", msg)

    r = host.transact(0x0001, 0x0301, 23, struct.pack(">H", 2048))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0301, 23)
    time.sleep(0.4)
    r = host.transact(0x0001, 0x0202, 24)
    _, _, data = host.verify_response(r, 0x0001, 0x0202, 24)
    ch1 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("0x0301 恢复2048→CH1≈1.65V", ok and 1.60 <= ch1 <= 1.70,
          "%.3fV" % ch1)

    # ---- 4c：阈值 ----
    r = host.transact(0x0001, 0x0401, 30)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0401, 30)
    if len(data) >= 8:
        l0, l1 = struct.unpack(">ff", data[:8])
        check("0x0401 读阈值", ok and abs(l0 - 2.5) < 0.01 and abs(l1 - 10.5) < 0.01,
              "%.2f / %.2f" % (l0, l1))
    else:
        check("0x0401 读阈值", False, msg)

    r = host.transact(0x0001, 0x0402, 31, struct.pack(">f", 3.2))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0402, 31)
    r = host.transact(0x0001, 0x0401, 32)
    _, _, data = host.verify_response(r, 0x0001, 0x0401, 32)
    l0 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("0x0402 写CH0阈值→回读", ok and abs(l0 - 3.2) < 0.01, "%.2f" % l0)

    r = host.transact(0x0001, 0x0402, 33, struct.pack(">f", 2.5))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0402, 33)
    check("0x0402 恢复2.50", ok, msg)

    r = host.transact(0x0001, 0x0402, 34, struct.pack(">f", 501.0))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0402, 34)
    check("0x0402 阈值501→0x04", ok and msg == "ERROR:04(非法参数值)", msg)

    # ---- 4c：变比（双写路径） ----
    r = host.transact(0x0001, 0x0201, 40)
    _, _, data = host.verify_response(r, 0x0001, 0x0201, 40)
    before = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0

    r = host.transact(0x0001, 0x0404, 41, struct.pack(">f", 2.0))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0404, 41)
    time.sleep(0.4)
    r = host.transact(0x0001, 0x0201, 42)
    _, _, data = host.verify_response(r, 0x0001, 0x0201, 42)
    after = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    doubled = (before > 0.05) and abs(after - 2.0 * before) < 0.05
    check("0x0404 变比×2即时生效", ok and doubled,
          "%.3f→%.3fV" % (before, after))

    r = host.transact(0x0001, 0x0404, 43, struct.pack(">f", 1.0))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0404, 43)
    check("0x0404 恢复1.00", ok, msg)

    r = host.transact(0x0001, 0x0404, 44, struct.pack(">f", 101.0))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0404, 44)
    check("0x0404 变比101→0x04", ok and msg == "ERROR:04(非法参数值)", msg)

    # ---- 4c：广播 ----
    r = host.transact(0xFFFF, 0x0301, 50, struct.pack(">H", 0x0200),
                      expect_reply=False)
    time.sleep(0.4)
    r = host.transact(0x0001, 0x0202, 51)
    _, _, data = host.verify_response(r, 0x0001, 0x0202, 51)
    ch1 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("广播DAC写静默执行", 0.35 <= ch1 <= 0.48, "CH1=%.3fV" % ch1)

    r = host.transact(0xFFFF, 0x0301, 52, struct.pack(">H", 2048),
                      expect_reply=False)
    time.sleep(0.4)
    r = host.transact(0x0001, 0x0202, 53)
    _, _, data = host.verify_response(r, 0x0001, 0x0202, 53)
    ch1 = struct.unpack(">f", data[:4])[0] if len(data) >= 4 else 0.0
    check("广播DAC恢复2048", 1.60 <= ch1 <= 1.70, "CH1=%.3fV" % ch1)

    r = host.transact(0xFFFF, 0x0104, 54, struct.pack(">H", 0x0008),
                      expect_reply=False)
    time.sleep(0.1)
    r = host.transact(0x0001, 0x0103, 55)
    _, _, data = host.verify_response(r, 0x0001, 0x0103, 55)
    check("广播改ID被拒绝(静默)", data.hex() == "0001", "id=%s" % data.hex())

    r = host.transact(0x0002, 0x0103, 56, expect_reply=False)
    check("错误地址静默", r is None, "无应答" if r is None else "意外应答!")

    # ---- 4c：TF / 自检 ----
    r = host.transact(0x0001, 0x0701, 60)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0701, 60)
    check("0x0701 TF状态", ok and len(data) == 1, "state=%s" % data.hex())

    r = host.transact(0x0001, 0x0801, 61)
    ok, msg, data = host.verify_response(r, 0x0001, 0x0801, 61)
    check("0x0801 系统自检", ok and len(data) == 4 and
          data[0] == 1 and data[1] == 1 and data[3] == 1,
          "oled=%d flash=%d tf=%d rtc=%d" % tuple(data) if len(data) == 4 else msg)

    # ---- K-01/K-02：坏帧错误应答 ----
    bad = break_crc(build_frame(0x0001, TYPE_COMMAND, 0x0103, 70))
    r = host.transact(0x0001, 0x0103, 70, raw_frame=bad)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0103, 70) if r else (False, "超时", b"")
    check("K-01 CRC坏帧→错误0x01", ok and msg == "ERROR:01(CRC错误)", msg)

    bad_len = bytearray(build_frame(0x0001, TYPE_COMMAND, 0x0103, 71))
    bad_len[10] = 0xFF
    bad_len[11] = 0xFF
    del bad_len[12:]                 # 只留 12B 头，长度字段声称 0xFFFF
    bad_len[-2:] = b"\xB6\xA5"       # 补一个"帧尾"便于固件判定
    r = host.transact(0x0001, 0x0103, 71, raw_frame=bytes(bad_len))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0103, 71) if r else (False, "超时", b"")
    check("K-02 长度错→错误0x02", ok and msg == "ERROR:02(长度错误)", msg)

    # ---- M4-4d：自动上报 ----
    r = host.transact(0x0001, 0x0304, 80, struct.pack(">H", 2))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0304, 80)
    check("0x0304 设上报间隔2s", ok, msg)

    r = host.transact(0x0001, 0x0302, 81)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0302, 81)
    check("0x0302 启动自动上报", ok, msg)

    events = host.drain_events(2.6)
    reports = [e for e in events if e[0] == 0x0382]
    h01_ok = len(reports) >= 1
    detail = "%d帧" % len(reports)
    if h01_ok:
        pl = reports[-1][1]
        if len(pl) == 12:
            ts = struct.unpack(">I", pl[0:4])[0]
            v0, v1 = struct.unpack(">ff", pl[4:12])
            h01_ok = 1700000000 <= ts <= 1900000000 and \
                     0.0 <= v0 <= 3.5 and 1.5 <= v1 <= 1.8
            detail += " ts=%d ch0=%.3f ch1=%.3f" % (ts, v0, v1)
        else:
            h01_ok = False
            detail += " 载荷长度%d≠12" % len(pl)
    check("H-01 自动上报帧格式", h01_ok, detail)

    r = host.transact(0x0001, 0x0201, 82)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0201, 82)
    check("H-02 上报期间查询被拒0x05", ok and msg == "ERROR:05(设备忙)", msg)

    r = host.transact(0x0001, 0x0303, 83)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0303, 83)
    check("0x0303 停止上报", ok, msg)

    leftover = host.drain_events(3.0)
    still = [e for e in leftover if e[0] == 0x0382]
    check("停止后无残留上报帧", len(still) == 0, "%d帧" % len(still))

    r = host.transact(0x0001, 0x0201, 84)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0201, 84)
    check("停止后查询恢复OK", ok and msg == "OK", msg)

    r = host.transact(0x0001, 0x0304, 85, struct.pack(">H", 5))
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0304, 85)
    check("0x0304 恢复间隔5s", ok, msg)

    # ---- 0x0101 重启（放最后：会断链几秒）----
    r = host.transact(0x0001, 0x0101, 90)
    ok, msg, _ = host.verify_response(r, 0x0001, 0x0101, 90)
    check("0x0101 重启应答OK", ok, msg)

    # 启动要挂 TF/OLED/Flash，实测约 5.5s，轮询最长 20s
    recovered = False
    boot_t0 = time.time()
    while time.time() - boot_t0 < 20.0:
        time.sleep(0.5)
        r = host.transact(0x0001, 0x0103, 91)
        if r is not None:
            ok, msg, data = host.verify_response(r, 0x0001, 0x0103, 91)
            recovered = ok and data.hex() == "0001"
            print("  [INFO] %.1fs 恢复应答 id=%s" % (time.time() - boot_t0,
                                                     data.hex()))
            break

    check("重启后恢复应答", recovered,
          "%.1fs" % (time.time() - boot_t0))

    passed = sum(1 for c in results if c)
    print("=== %d/%d PASS ===" % (passed, len(results)))
    return passed == len(results)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    host = Rs485Host(sys.argv[1])
    mode = sys.argv[2]

    if mode == "all":
        sys.exit(0 if run_all(host) else 1)

    if mode == "one":
        command = int(sys.argv[3], 16)
        resp = host.transact(0x0001, command, 0x0001)
        ok, msg, data = host.verify_response(resp, 0x0001, command, 0x0001)
        print("%s %s data=%s" % ("OK" if ok else "FAIL", msg, data.hex()))
        return

    if mode == "heartbeat":
        # 七-7：上电发一次后每 30s 一次。上电后运行此模式，等 35s 应至少收到一帧
        events = host.drain_events(35.0)
        beats = [e for e in events if e[0] == 0x8888]
        print("收到心跳 %d 帧" % len(beats))
        ok = len(beats) >= 1 and beats[0][1].hex() == "0001"
        print("A-04 心跳ID一致:", "PASS" if ok else "FAIL")
        sys.exit(0 if ok else 1)

    print("未知模式:", mode)
    sys.exit(1)


if __name__ == "__main__":
    main()
