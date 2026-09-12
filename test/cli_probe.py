import sys, time, serial

port = sys.argv[1]
commands = sys.argv[2:]

ser = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.3)

# DTR pulse to reset the target (device resets when DTR toggles)
ser.setDTR(True)
time.sleep(0.2)
ser.setDTR(False)

data = b""
end = time.time() + 5.0
while time.time() < end:
    chunk = ser.read(ser.in_waiting or 1)
    if chunk:
        data += chunk
        end = time.time() + 0.8
print("--- boot banner ---")
print(data.decode("utf-8", "replace"))

for cmd in commands:
    print(f"\n>>> {cmd}", flush=True)
    ser.write(cmd.encode("ascii") + b"\r\n")
    data = b""
    end = time.time() + 3.0
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            data += chunk
            end = time.time() + 0.8
    print(data.decode("utf-8", "replace"), flush=True)

ser.close()
