#!/usr/bin/env python3
"""Send reproducible JustCanFd normal or fast test frames over UDP."""

import argparse
import math
import socket
import struct
import time


NORMAL_CAN_ID = 0x0400
FAST_CAN_ID = 0x0600
MAX_PAYLOAD_LEN = 64


def parse_args():
    parser = argparse.ArgumentParser(
        description="Send AxDr JustCanFd test frames to VOFA+ over UDP."
    )
    parser.add_argument("--mode", choices=("normal", "fast"), required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1347)
    parser.add_argument("--count", type=int, default=150)
    parser.add_argument("--interval-ms", type=float, default=20.0)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--samples-per-packet", type=int, default=8)
    parser.add_argument(
        "--amplitude",
        type=float,
        help="Defaults to 1.0 in normal mode and 10000 in fast mode.",
    )
    return parser.parse_args()


def validate_args(args):
    if not 1 <= args.port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    if args.count <= 0:
        raise ValueError("count must be greater than zero")
    if args.interval_ms < 0:
        raise ValueError("interval-ms must not be negative")

    if args.mode == "normal":
        if not 1 <= args.channels <= 15:
            raise ValueError("normal mode supports 1 to 15 channels")
        payload_len = 4 + args.channels * 4
    else:
        if not 1 <= args.channels <= 8:
            raise ValueError("fast mode supports 1 to 8 channels")
        if not 1 <= args.samples_per_packet <= 255:
            raise ValueError("samples-per-packet must be between 1 and 255")
        payload_len = 4 + args.samples_per_packet * args.channels * 2

    if payload_len > MAX_PAYLOAD_LEN:
        raise ValueError(
            f"payload length {payload_len} exceeds {MAX_PAYLOAD_LEN} bytes"
        )


def channel_value(phase, channel, channel_count):
    phase_offset = channel * math.pi / max(channel_count, 2)
    return math.sin(phase + phase_offset)


def build_normal_packet(packet_index, args):
    amplitude = 1.0 if args.amplitude is None else args.amplitude
    phase = packet_index * 0.05
    values = [
        amplitude * channel_value(phase, channel, args.channels)
        for channel in range(args.channels)
    ]
    metadata = packet_index.to_bytes(4, "little")[:3]
    payload = metadata + bytes([args.channels]) + struct.pack(
        "<" + "f" * len(values), *values
    )
    return b"AXDR" + struct.pack("<HB", NORMAL_CAN_ID, len(payload)) + payload


def build_fast_packet(packet_index, args):
    amplitude = 10000.0 if args.amplitude is None else args.amplitude
    if not 0 <= amplitude <= 32767:
        raise ValueError("fast mode amplitude must be between 0 and 32767")

    values = []
    first_sample = packet_index * args.samples_per_packet
    for sample in range(args.samples_per_packet):
        phase = (first_sample + sample) * 0.05
        for channel in range(args.channels):
            values.append(
                int(amplitude * channel_value(phase, channel, args.channels))
            )

    metadata = packet_index.to_bytes(4, "little")[:3]
    payload = metadata + bytes([args.samples_per_packet]) + struct.pack(
        "<" + "h" * len(values), *values
    )
    return b"AXDR" + struct.pack("<HB", FAST_CAN_ID, len(payload)) + payload


def main():
    args = parse_args()
    validate_args(args)

    build_packet = (
        build_normal_packet if args.mode == "normal" else build_fast_packet
    )
    target = (args.host, args.port)
    sent_bytes = 0

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for packet_index in range(args.count):
            packet = build_packet(packet_index, args)
            sent_bytes += sock.sendto(packet, target)
            if args.interval_ms:
                time.sleep(args.interval_ms / 1000.0)

    samples_per_packet = 1 if args.mode == "normal" else args.samples_per_packet
    print(f"mode={args.mode}")
    print(f"target={args.host}:{args.port}")
    print(f"sent_packets={args.count}")
    print(f"samples_per_packet={samples_per_packet}")
    print(f"channels={args.channels}")
    print(f"parsed_samples={args.count * samples_per_packet}")
    print(f"sent_bytes={sent_bytes}")


if __name__ == "__main__":
    main()
