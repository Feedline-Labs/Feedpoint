"""
Feedpoint example: find a dongle, tune it, read IQ and report what it hears.

Uses only the Python standard library. Run it with the path to feedpoint:

    python tune_and_read.py ./feedpoint          (feedpoint.exe on Windows)

The driver must sit beside feedpoint, as in the release downloads.
Protocol: ../PROTOCOL.md
"""

import math
import struct
import subprocess
import sys
from enum import IntEnum

FREQUENCY = 162_400_000     # Hz; NOAA Weather Radio. Change to a strong local signal.
SAMPLE_RATE = 2_048_000     # samples per second
READ_BYTES = 262_144        # 128k IQ pairs, about 64 ms; a multiple of 512


class Op(IntEnum):              # matches PROTOCOL.md
    UNASSIGNED = 0
    HELLO = 1
    LIST = 2
    OPEN = 3
    CLOSE = 4
    SET_SAMPLE_RATE = 5
    SET_CENTER_FREQUENCY = 6
    SET_TUNER_BANDWIDTH = 7
    SET_AGC_MODE = 8
    SET_TUNER_GAIN_MODE = 9
    SET_TUNER_GAIN = 10
    RESET_BUFFER = 11
    READ_SYNC = 12
    GET_VERSION = 13
    GET_TUNER_GAINS = 14
    GET_TUNER_GAIN = 15
    GET_SAMPLE_RATE = 16
    GET_CENTER_FREQUENCY = 17
    GET_TUNER_TYPE = 18
    SET_FREQUENCY_CORRECTION = 19
    GET_FREQUENCY_CORRECTION = 20
    SET_BIAS_TEE = 21
    SET_BIAS_TEE_GPIO = 22
    SET_DIRECT_SAMPLING = 23
    GET_DIRECT_SAMPLING = 24
    SET_OFFSET_TUNING = 25
    GET_OFFSET_TUNING = 26
    SET_XTAL_FREQUENCY = 27
    GET_XTAL_FREQUENCY = 28
    SET_TUNER_IF_GAIN = 29
    GET_INDEX_BY_SERIAL = 30
    GET_DEVICE_NAME = 31
    GET_USB_STRINGS = 32


TUNER_NAMES = ["unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D"]


class Payload:
    """Reads the little-endian values and strings in a response payload."""

    def __init__(self, data):
        self.data = data
        self.position = 0

    def u32(self):
        (value,) = struct.unpack_from("<I", self.data, self.position)
        self.position += 4
        return value

    def i32(self):
        (value,) = struct.unpack_from("<i", self.data, self.position)
        self.position += 4
        return value

    def string(self):
        length = self.u32()
        text = self.data[self.position:self.position + length].decode("utf-8", "replace")
        self.position += length
        return text


class Feedpoint:
    """One Feedpoint process: one request, one response, in order."""

    def __init__(self, path):
        self.process = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def request(self, op, *values):
        """Sends op with u32/i32 values; returns (status, Payload)."""
        argument = b"".join(struct.pack("<i" if v < 0 else "<I", v) for v in values)
        self.process.stdin.write(struct.pack("<BI", op, len(argument)) + argument)
        self.process.stdin.flush()
        header = self.process.stdout.read(8)
        if len(header) < 8:
            raise RuntimeError("Feedpoint exited; is the driver beside it?")
        status, length = struct.unpack("<iI", header)
        return status, Payload(self.process.stdout.read(length))

    def call(self, op, *values):
        """Like request, but raises unless the status is 0."""
        status, payload = self.request(op, *values)
        if status != 0:
            raise RuntimeError(f"{op.name} failed: {status}")
        return payload

    def close(self):
        self.process.stdin.close()      # Feedpoint closes the dongle and exits
        self.process.wait()


def main():
    feedpoint = Feedpoint(sys.argv[1] if len(sys.argv) > 1 else "./feedpoint")
    try:
        status, payload = feedpoint.request(Op.GET_VERSION)
        version = payload.string() if status == 0 else "1.0 (operations 1-12 only)"
        hello = feedpoint.call(Op.HELLO)
        print(f"Feedpoint {version}, protocol {hello.u32()}")
        print(f"Driver {hello.string()}")

        dongles = feedpoint.call(Op.LIST)
        count = dongles.u32()
        for index in range(count):
            vendor, product, serial = dongles.string(), dongles.string(), dongles.string()
            print(f"Dongle {index}: {vendor} {product}, serial {serial}")
        if count == 0:
            print("No RTL-SDR dongle found.")
            return

        feedpoint.call(Op.OPEN, 0)
        tuner = feedpoint.call(Op.GET_TUNER_TYPE).u32()
        gains = feedpoint.call(Op.GET_TUNER_GAINS)
        gain_list = [gains.i32() for _ in range(gains.u32())]
        print(f"Tuner {TUNER_NAMES[tuner]}, {len(gain_list)} gains from "
              f"{gain_list[0] / 10:.1f} to {gain_list[-1] / 10:.1f} dB")

        gain = gain_list[len(gain_list) // 2]   # a gain from the middle of the list
        feedpoint.call(Op.SET_SAMPLE_RATE, SAMPLE_RATE)
        feedpoint.call(Op.SET_CENTER_FREQUENCY, FREQUENCY)
        feedpoint.call(Op.SET_TUNER_GAIN_MODE, 1)   # manual
        feedpoint.call(Op.SET_TUNER_GAIN, gain)

        rate = feedpoint.call(Op.GET_SAMPLE_RATE).u32()
        frequency = feedpoint.call(Op.GET_CENTER_FREQUENCY).u32()
        gain_set = feedpoint.call(Op.GET_TUNER_GAIN).i32()
        print(f"Tuned to {frequency / 1e6:.3f} MHz at {rate:,} samples/s, gain {gain_set / 10:.1f} dB")

        feedpoint.call(Op.RESET_BUFFER)
        feedpoint.call(Op.READ_SYNC, READ_BYTES)   # discard the first block after a reset
        iq = feedpoint.call(Op.READ_SYNC, READ_BYTES).data

        # Unsigned 8-bit I/Q pairs centered on 127.5. Level is average power
        # relative to full scale (0 dBFS); DC offset is the average I and Q.
        i_values = [b - 127.5 for b in iq[0::2]]
        q_values = [b - 127.5 for b in iq[1::2]]
        power = sum(i * i + q * q for i, q in zip(i_values, q_values)) / len(i_values)
        level = 10 * math.log10(power / (127.5 * 127.5))
        print(f"Read {len(iq):,} bytes: level {level:.1f} dBFS, DC offset "
              f"I {sum(i_values) / len(i_values):+.2f} / Q {sum(q_values) / len(q_values):+.2f}")
    finally:
        feedpoint.close()


if __name__ == "__main__":
    main()
