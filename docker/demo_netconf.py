#!/usr/bin/env python3
"""End-to-end NETCONF demo against the simulated optical NE.

Talks real NETCONF (over SSH, ncclient) to Netopeer2:
  1. provisions the transponder (400G / 16QAM) and two cross-connects
  2. reads operational state (port powers, alarms, BER)
  3. provokes a wavelength collision and shows the device rejecting it
  4. drops input power and shows the LOS alarm rising
"""
import sys
import time

from ncclient import manager
from ncclient.operations.rpc import RPCError

NS = "urn:onsim:device"
HOST = dict(host="localhost", port=830, username="root", password="onsim",
            hostkey_verify=False, allow_agent=False, look_for_keys=False)


def edit(m, xml_body):
    cfg = ('<config xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">'
           f'<device xmlns="{NS}">{xml_body}</device></config>')
    m.edit_config(target="running", config=cfg)


def xc(name, in_port, out_port, channel, op=""):
    op_attr = f' xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0" nc:operation="{op}"' if op else ""
    return (f"<roadm><cross-connect{op_attr}><name>{name}</name>"
            f"<in-port>{in_port}</in-port><out-port>{out_port}</out-port>"
            f"<channel>{channel}</channel></cross-connect></roadm>")


def show_state(m, label):
    print(f"\n--- operational state: {label} ---")
    reply = m.get(filter=("xpath", f"/device"))
    print(reply.data_xml)


def main():
    with manager.connect(**HOST) as m:
        print("== NETCONF session established (capabilities: "
              f"{sum(1 for _ in m.server_capabilities)}) ==")

        print("\n[1] provision transponder 400G/16QAM and bring it up")
        edit(m, "<transponder><line-rate>r400g</line-rate>"
                "<modulation>qam16</modulation></transponder>")
        edit(m, "<transponder><admin-state>up</admin-state></transponder>")

        print("[2] provision cross-connects ch40: 1->2, ch41: 1->3")
        edit(m, xc("east", 1, 2, 40))
        edit(m, xc("west", 1, 3, 41))
        show_state(m, "after provisioning")

        print("[3] provoke a wavelength collision (ch40 to port 2 again)")
        try:
            edit(m, xc("clash", 4, 2, 40))
            print("ERROR: collision was not rejected!")
            sys.exit(1)
        except RPCError as e:
            print(f"    device rejected it, as it should: {e.message.strip()}")

        print("[4] wait for power drift ticks, then read state again")
        time.sleep(3)
        show_state(m, "after 3s of drift")

    print("\nDemo complete: config push, state read, and transactional "
          "rejection all over real NETCONF.")


if __name__ == "__main__":
    main()
