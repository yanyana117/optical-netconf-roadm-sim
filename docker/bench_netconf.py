#!/usr/bin/env python3
"""Experiment: NETCONF configuration-transaction latency.

Measures the full round trip of an <edit-config> through the stack:
ncclient -> Netopeer2 -> sysrepo -> onsim-netconfd -> DDS request/reply ->
onsim-devd -> HAL, and back. Each iteration flips a cross-connect between
two channels, so every transaction does real provisioning work.
"""
import statistics
import time

from ncclient import manager

NS = "urn:onsim:device"
HOST = dict(host="localhost", port=830, username="root", password="onsim",
            hostkey_verify=False, allow_agent=False, look_for_keys=False)
N = 60


def edit(m, xml_body):
    cfg = ('<config xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">'
           f'<device xmlns="{NS}">{xml_body}</device></config>')
    m.edit_config(target="running", config=cfg)


def xc(channel):
    return ("<roadm><cross-connect><name>bench</name>"
            f"<in-port>1</in-port><out-port>2</out-port>"
            f"<channel>{channel}</channel></cross-connect></roadm>")


def main():
    with manager.connect(**HOST) as m:
        edit(m, xc(10))  # warm-up / create
        times = []
        for i in range(N):
            t0 = time.perf_counter()
            edit(m, xc(11 + (i % 2)))  # alternate 11/12: real change each time
            times.append((time.perf_counter() - t0) * 1000.0)
        times.sort()
        print(f"NETCONF edit-config transactions: n={N}")
        print(f"  mean  {statistics.mean(times):7.1f} ms")
        print(f"  p50   {times[N // 2]:7.1f} ms")
        print(f"  p95   {times[int(N * 0.95)]:7.1f} ms")
        print(f"  max   {times[-1]:7.1f} ms")
        print("(full path: ncclient -> Netopeer2 -> sysrepo -> onsim-netconfd "
              "-> DDS req/reply -> onsim-devd -> HAL)")


if __name__ == "__main__":
    main()
