import sys
import time

import serial


port = sys.argv[1] if len(sys.argv) > 1 else "COM9"

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()
time.sleep(0.3)
ser.reset_input_buffer()


def send(command, wait_seconds):
    ser.write(command.encode("ascii") + b"\r\n")
    end = time.time() + wait_seconds
    data = b""
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            data += chunk
    print(">>> " + command)
    print(data.decode("utf-8", "replace"))
    return data


def collect(seconds, label):
    end = time.time() + seconds
    data = b""
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            data += chunk
    print("--- " + label + " ---")
    print(data.decode("utf-8", "replace"))
    return data


send("limit ch0", 0.8)
send("0", 0.8)
send("start", 0.8)
collect(22.0, "sampling window")
send("limit ch0", 0.8)
send("3.00", 0.8)
collect(1.5, "recovery window")
send("stop", 1.0)

ser.close()
