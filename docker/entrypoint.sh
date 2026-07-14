#!/bin/bash
# Boot the simulated network element: install the YANG model, start the
# device daemon and the Netopeer2 NETCONF server, then either run the demo
# (default) or drop to a shell / hold the NE up.
set -e

echo "== installing YANG module into sysrepo =="
sysrepoctl -i /onsim/yang/onsim-device.yang 2>/dev/null || \
    sysrepoctl -U /onsim/yang/onsim-device.yang

echo "== starting onsim-netconfd (device daemon) =="
onsim-netconfd &
sleep 1

echo "== starting netopeer2-server (NETCONF over SSH, port 830) =="
netopeer2-server -d -v1 &
sleep 2

# NETCONF auth for the demo: local root with a known password.
echo 'root:onsim' | chpasswd

case "${1:-demo}" in
  demo)
    python3 /onsim/docker/demo_netconf.py
    ;;
  shell)
    exec bash
    ;;
  serve)
    echo "NE is up; NETCONF on port 830 (root/onsim). Ctrl-C to stop."
    wait
    ;;
esac
