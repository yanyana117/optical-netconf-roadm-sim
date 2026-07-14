#!/usr/bin/env python3
"""Subscribe to the NE's protobuf telemetry stream (ZeroMQ PUB/SUB).

Usage: telemetry_sub.py [--endpoint tcp://localhost:5556] [--samples N]
"""
import argparse
import sys

import zmq

import onsim_telemetry_pb2 as pb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", default="tcp://localhost:5556")
    ap.add_argument("--samples", type=int, default=0, help="exit after N (0 = forever)")
    args = ap.parse_args()

    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect(args.endpoint)
    sock.setsockopt_string(zmq.SUBSCRIBE, "telemetry")
    sock.setsockopt(zmq.RCVTIMEO, 10000)

    print(f"subscribed to {args.endpoint} (topic 'telemetry')")
    n = 0
    while args.samples == 0 or n < args.samples:
        try:
            topic, payload = sock.recv_multipart()
        except zmq.error.Again:
            print("timed out waiting for telemetry", file=sys.stderr)
            return 1
        sample = pb.TelemetrySample()
        sample.ParseFromString(payload)
        xp = sample.transponder
        ports = " ".join(
            f"p{p.number}:{p.output_power_dbm:+.2f}dBm{'!LOS' if p.los_alarm else ''}"
            for p in sample.ports)
        print(f"tick={sample.tick:<4} {ports}  "
              f"xpdr[{'up' if xp.admin_up else 'down'} {xp.line_rate_gbps}G "
              f"{pb.TransponderSample.Modulation.Name(xp.modulation)}] "
              f"osnr={xp.osnr_db:.2f}dB ber={xp.pre_fec_ber:.2e}"
              f"{' !DEGRADE' if xp.ber_degrade_alarm else ''}")
        n += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
