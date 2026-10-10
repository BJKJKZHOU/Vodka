#!/usr/bin/env python3
"""Send CAN-FD-DLC-correct AXDR telemetry test frames to VOFA+ over UDP."""

import argparse
import math
import socket
import struct
import time

RESPONSE_TYPE = 0x02
NORMAL_TYPE = 0x10
FAST_TYPE = 0x18
MAX_PAYLOAD_LEN = 64
FAST_TEST_VAR_IDS = (
    0x0001, 0x0002, 0x0011, 0x0010,
    0x0012, 0x0013, 0x0014, 0x0023,
)
CAN_FD_LENGTHS = (*range(9), 12, 16, 20, 24, 32, 48, 64)


def can_fd_len(required):
    for length in CAN_FD_LENGTHS:
        if required <= length:
            return length
    raise ValueError(f"CAN FD payload exceeds {MAX_PAYLOAD_LEN} bytes")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("normal", "fast"), required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1347)
    parser.add_argument("--count", type=int, default=150)
    parser.add_argument("--interval-ms", type=float, default=20.0)
    parser.add_argument("--node", type=int, default=1)
    parser.add_argument("--config-id", type=int, default=1)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--samples-per-packet", type=int, default=8)
    parser.add_argument("--amplitude", type=float)
    return parser.parse_args()


def validate_args(args):
    if not 1 <= args.port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    if not 1 <= args.node <= 63:
        raise ValueError("node must be between 1 and 63")
    if not 0 <= args.config_id <= 255:
        raise ValueError("config-id must be between 0 and 255")
    if args.count <= 0 or args.interval_ms < 0:
        raise ValueError("count must be positive and interval nonnegative")
    if args.mode == "normal":
        if not 1 <= args.channels <= 15:
            raise ValueError("normal supports 1..15 channels")
        can_fd_len(4 + args.channels * 4)
    else:
        if not 1 <= args.channels <= len(FAST_TEST_VAR_IDS):
            raise ValueError("fast supports 1..8 channels")
        if args.samples_per_packet <= 0:
            raise ValueError("samples-per-packet must be positive")
        can_fd_len(4 + args.samples_per_packet * args.channels * 2)


def channel_value(phase, channel, channel_count):
    return math.sin(phase + channel * math.pi / max(channel_count, 2))


def wrap_packet(msg_type, payload, node=1):
    data_len = can_fd_len(len(payload))
    can_id = (msg_type << 6) | node
    return b"AXDR" + struct.pack("<HB", can_id, data_len) + payload.ljust(data_len, b"\x00")


def fast_config_response(args):
    ids = FAST_TEST_VAR_IDS[: args.channels]
    payload = bytes((1, 0x04, 0x01, 0x00, 0x00, args.config_id, len(ids)))
    payload += struct.pack("<" + "H" * len(ids), *ids)
    return wrap_packet(RESPONSE_TYPE, payload, args.node)


def normal_packet(packet_index, args):
    amp = 1.0 if args.amplitude is None else args.amplitude
    values = [
        amp * channel_value(packet_index * 0.05, ch, args.channels)
        for ch in range(args.channels)
    ]
    payload = struct.pack("<HBB", packet_index & 0xFFFF, args.config_id, args.channels)
    payload += struct.pack("<" + "f" * len(values), *values)
    return wrap_packet(NORMAL_TYPE, payload, args.node)


def fast_packet(packet_index, args):
    amp = 10000.0 if args.amplitude is None else args.amplitude
    if not 0 <= amp <= 32767:
        raise ValueError("fast amplitude must be 0..32767 raw counts")
    values = []
    for sample in range(args.samples_per_packet):
        phase = (packet_index * args.samples_per_packet + sample) * 0.05
        for ch in range(args.channels):
            values.append(int(amp * channel_value(phase, ch, args.channels)))
    payload = struct.pack("<HBB", packet_index & 0xFFFF,
                          args.config_id, args.samples_per_packet)
    payload += struct.pack("<" + "h" * len(values), *values)
    return wrap_packet(FAST_TYPE, payload, args.node)


def main():
    args = parse_args()
    validate_args(args)
    make_packet = normal_packet if args.mode == "normal" else fast_packet
    target = (args.host, args.port)
    total = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        if args.mode == "fast":
            total += sock.sendto(fast_config_response(args), target)
            if args.interval_ms:
                time.sleep(args.interval_ms / 1000)
        for index in range(args.count):
            total += sock.sendto(make_packet(index, args), target)
            if args.interval_ms:
                time.sleep(args.interval_ms / 1000)
    print(f"mode={args.mode} target={args.host}:{args.port}")
    print(f"packets={args.count} channels={args.channels} node={args.node}")
    print(f"samples={args.count * (1 if args.mode == 'normal' else args.samples_per_packet)}")
    print(f"bytes_sent={total}")


if __name__ == "__main__":
    main()
