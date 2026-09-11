#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from rs485_host import build_modbus_frame, verify_modbus_response


class ModbusHostFrameTests(unittest.TestCase):
    def test_build_read_request_uses_modbus_crc_low_byte_first(self):
        frame = build_modbus_frame(
            0x01, 0x03, bytes.fromhex("00 00 00 02"))

        self.assertEqual(frame, bytes.fromhex("01 03 00 00 00 02 C4 0B"))

    def test_verify_read_response_returns_register_data(self):
        response = bytes.fromhex("01 03 04 3F 80 00 00 F7 CF")

        ok, message, data = verify_modbus_response(response, 0x01, 0x03)

        self.assertTrue(ok)
        self.assertEqual(message, "OK")
        self.assertEqual(data, bytes.fromhex("3F 80 00 00"))

    def test_verify_exception_response_returns_exception_code(self):
        response = bytes.fromhex("01 83 02 C0 F1")

        ok, message, data = verify_modbus_response(response, 0x01, 0x03)

        self.assertTrue(ok)
        self.assertEqual(message, "EXCEPTION:02(非法地址)")
        self.assertEqual(data, b"")

    def test_verify_response_rejects_bad_crc(self):
        response = bytes.fromhex("01 03 04 3F 80 00 00 F7 CE")

        ok, message, data = verify_modbus_response(response, 0x01, 0x03)

        self.assertFalse(ok)
        self.assertEqual(message, "CRC错误")
        self.assertEqual(data, b"")


if __name__ == "__main__":
    unittest.main()
