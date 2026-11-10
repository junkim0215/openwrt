#!/bin/sh
# Xiaomi BE5000: spread receive processing over both CPUs.
#
# The generic script puts the Wi-Fi and Ethernet NAPI threads wherever its
# load estimate lands, often on the same core. Here Wi-Fi (NAPI threads,
# the mt76 TX worker and RPS of the Wi-Fi interfaces) runs on CPU1 and
# Ethernet (NAPI threads, the QDMA interrupts and RPS of eth0/eth1) on CPU0,
# so both halves of a bridged, routed or tunnelled flow are processed in
# parallel. This is the stock layout with Wi-Fi; with the WAN port on its own
# QDMA (airoha_eth.wan_qdma1) its interrupt goes to CPU0 as well.
#
# $1 is network.globals.packet_steering; 0 keeps steering disabled.

generic=/usr/libexec/network/packet-steering.uc

case "$(cat /tmp/sysinfo/board_name 2>/dev/null)" in
xiaomi,be5000|iptime,be5100m|iptime,be3600p) ;;
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

# QDMA0 and QDMA1 interrupts (airoha_eth.0 and airoha_eth.4)
for irq in $(awk -F: '/airoha_eth\.[04]$/ { gsub(/ /, "", $1); print $1 }' /proc/interrupts); do
	echo 1 > "/proc/irq/$irq/smp_affinity" 2>/dev/null
done

for d in /sys/class/net/*; do
	q="$d/queues/rx-0/rps_cpus"
	[ -w "$q" ] || continue

	# the QDMA records its RX ring as the queue: set every queue of eth*
	case "${d##*/}" in
	eth0|eth1) for q in "$d"/queues/rx-*/rps_cpus; do echo 1 > "$q"; done ;;
	*) [ -e "$d/phy80211" ] && echo 2 > "$q" ;;
	esac
done

exit 0
