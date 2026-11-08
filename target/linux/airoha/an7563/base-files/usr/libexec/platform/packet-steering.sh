#!/bin/sh
# Xiaomi BE5000: spread receive processing over both CPUs.
#
# The generic script puts the Wi-Fi and Ethernet NAPI threads wherever its
# load estimate lands, often on the same core. Here Wi-Fi (NAPI threads,
# the mt76 TX worker and RPS of the Wi-Fi interfaces) runs on CPU1 and
# Ethernet (NAPI threads and RPS of eth0/eth1) on CPU0, so both halves of a
# bridged, routed or tunnelled flow are processed in parallel.
#
# $1 is network.globals.packet_steering; 0 keeps steering disabled.

generic=/usr/libexec/network/packet-steering.uc

case "$(cat /tmp/sysinfo/board_name 2>/dev/null)" in
xiaomi,be5000) ;;
*) exec "$generic" "$@" ;;
esac

[ "$1" = 0 ] && exec "$generic" 0

# defaults for every other device first
"$generic" "$@" >/dev/null 2>&1

for p in /proc/[0-9]*; do
	{ read -r comm < "$p/comm"; } 2>/dev/null || continue
	case "$comm" in
	napi/phy*|mt76-tx*) taskset -p 2 "${p#/proc/}" >/dev/null 2>&1 ;;
	napi/qdma*) taskset -p 1 "${p#/proc/}" >/dev/null 2>&1 ;;
	esac
done

for d in /sys/class/net/*; do
	q="$d/queues/rx-0/rps_cpus"
	[ -w "$q" ] || continue

	case "${d##*/}" in
	eth0|eth1) echo 1 > "$q" ;;
	*) [ -e "$d/phy80211" ] && echo 2 > "$q" ;;
	esac
done

exit 0
