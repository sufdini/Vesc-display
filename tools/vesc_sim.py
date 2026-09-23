#!/usr/bin/env python3
"""Fake VESC for bench-testing the display without a motor controller.

Answers COMM_GET_VALUES requests on a serial port with a synthetic ride:
speed ramps up and down, battery slowly drains, temperatures rise with load.

    pip install pyserial
    python3 tools/vesc_sim.py /dev/ttyUSB1            # 115200 baud by default
    python3 tools/vesc_sim.py /dev/ttyUSB1 --fault 5  # report OVER_TEMP_FET

Wire the adapter's TX to the display's VESC_RX_PIN and RX to VESC_TX_PIN.
"""
import argparse
import math
import struct
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")

COMM_GET_VALUES = 4


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def frame(payload: bytes) -> bytes:
    if len(payload) < 256:
        head = bytes([2, len(payload)])
    else:
        head = bytes([3]) + struct.pack(">H", len(payload))
    return head + payload + struct.pack(">H", crc16(payload)) + bytes([3])


def build_values(t: float, fault: int, poles: int) -> bytes:
    """Synthesize a COMM_GET_VALUES payload for time t (seconds)."""
    # Speed profile: accelerate, cruise, brake (regen), stop; 40 s cycle.
    phase = t % 40.0
    if phase < 10:
        motor_rpm = 900 * phase / 10
    elif phase < 25:
        motor_rpm = 900 + 100 * math.sin(phase)
    elif phase < 32:
        motor_rpm = 900 * (32 - phase) / 7
    else:
        motor_rpm = 0
    erpm = motor_rpm * poles / 2
    braking = 25 <= phase < 32 and motor_rpm > 50
    duty = min(motor_rpm / 1000.0, 0.95)
    current_in = -6.0 if braking else 3.0 + 25.0 * duty
    current_motor = current_in * 1.6
    voltage = 41.5 - 0.02 * t - 0.05 * max(current_in, 0)
    temp_fet = 32 + 20 * duty + 0.05 * t
    temp_motor = 28 + 35 * duty + 0.08 * t
    ah = 0.0004 * t
    ah_charged = 0.00003 * t
    wh = ah * 40
    wh_charged = ah_charged * 40
    tacho = int(t * motor_rpm * 3 * poles / 60)

    p = bytearray([COMM_GET_VALUES])
    p += struct.pack(">h", int(temp_fet * 10))
    p += struct.pack(">h", int(temp_motor * 10))
    p += struct.pack(">i", int(current_motor * 100))
    p += struct.pack(">i", int(current_in * 100))
    p += struct.pack(">i", 0)                       # id
    p += struct.pack(">i", int(current_motor * 100))  # iq
    p += struct.pack(">h", int(duty * 1000))
    p += struct.pack(">i", int(erpm))
    p += struct.pack(">h", int(voltage * 10))
    p += struct.pack(">i", int(ah * 10000))
    p += struct.pack(">i", int(ah_charged * 10000))
    p += struct.pack(">i", int(wh * 10000))
    p += struct.pack(">i", int(wh_charged * 10000))
    p += struct.pack(">i", tacho)
    p += struct.pack(">i", abs(tacho))
    p += bytes([fault])
    p += struct.pack(">i", 0)                       # pid pos
    p += bytes([42])                                # controller id
    p += struct.pack(">hhh", int(temp_fet * 10), int(temp_fet * 10 + 5), int(temp_fet * 10 - 5))
    p += struct.pack(">ii", 0, 0)                   # vd, vq
    p += bytes([0])                                 # status
    return bytes(p)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--fault", type=int, default=0, help="mc_fault_code to report (0 = none)")
    ap.add_argument("--poles", type=int, default=14)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    print(f"Fake VESC on {args.port} @ {args.baud}, Ctrl-C to stop")
    t0 = time.time()
    buf = bytearray()
    replies = 0
    while True:
        buf += ser.read(64)
        # Look for the 6 byte COMM_GET_VALUES request frame: 02 01 04 40 84 03
        while True:
            idx = buf.find(bytes([2, 1, COMM_GET_VALUES]))
            if idx < 0 or len(buf) < idx + 6:
                if len(buf) > 256:
                    del buf[:-8]
                break
            del buf[: idx + 6]
            ser.write(frame(build_values(time.time() - t0, args.fault, args.poles)))
            replies += 1
            if replies % 50 == 0:
                print(f"\r{replies} replies", end="", flush=True)


if __name__ == "__main__":
    main()
