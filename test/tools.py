import sys, serial

NUM = 8
SERVO_MIN_US, SERVO_MAX_US = 500, 2500
angles = [90.0] * NUM

def to_us(deg):
    deg = max(0.0, min(180.0, deg))
    return SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * deg / 180.0

port = sys.argv[1] if len(sys.argv) > 1 else "/tmp/vesp_emu"
ser = serial.Serial(port, 115200, timeout=0.2)
buf = b""
import serial, sys, time

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
ser = serial.Serial(port, 115200, timeout=0.2)
print(f"sender on {port}")

while True:
    s = input("cmd> ")          # 例: 3 120 / S 90 80 100 95 90 85 100 90
    if s in ("q", "quit"):
        break
    ser.write((s + "\n").encode())
    time.sleep(0.1)
    if ser.in_waiting:           # 実機からの応答を表示
        print(ser.read(ser.in_waiting).decode(errors="ignore"), end="")