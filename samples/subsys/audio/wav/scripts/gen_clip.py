#!/usr/bin/env python3
#
# Copyright (c) 2026 Ugo Marchand
#
# SPDX-License-Identifier: Apache-2.0

"""Write a small RIFF/WAVE container for the sample to parse.

The clip is generated rather than committed because binary files are not allowed in the
tree. It deliberately carries a LIST/INFO chunk between the format and the payload, the way
every common encoder does, so that the audio does not start at the offset 44 that so much
example code assumes.
"""

import argparse
import math
import struct

SAMPLE_RATE = 8000
CHANNELS = 1
BITS = 16
TONE_HZ = 440
DURATION_MS = 200
AMPLITUDE = 0.8


def chunk(ident: bytes, body: bytes) -> bytes:
    """A RIFF chunk: identifier, size, body, and a pad byte to an even length."""
    return ident + struct.pack("<I", len(body)) + body + (b"\x00" if len(body) % 2 else b"")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("output", help="path of the .wav file to write")
    args = parser.parse_args()

    frames = (SAMPLE_RATE * DURATION_MS) // 1000
    peak = int(AMPLITUDE * 32767)
    samples = bytearray()

    for i in range(frames):
        value = peak * math.sin(2 * math.pi * TONE_HZ * i / SAMPLE_RATE)
        samples += struct.pack("<h", int(value))

    block_align = CHANNELS * BITS // 8
    fmt = struct.pack(
        "<HHIIHH",
        1,  # WAVE_FORMAT_PCM
        CHANNELS,
        SAMPLE_RATE,
        SAMPLE_RATE * block_align,
        block_align,
        BITS,
    )
    info = b"INFO" + chunk(b"ISFT", b"Zephyr WAV sample\x00")

    body = b"WAVE" + chunk(b"fmt ", fmt) + chunk(b"LIST", info) + chunk(b"data", bytes(samples))

    with open(args.output, "wb") as out:
        out.write(chunk(b"RIFF", body))


if __name__ == "__main__":
    main()
