# SPDX-License-Identifier: GPL-2.0-only

# Attach the "ubi-data" partition of the Xiaomi BE5000, shared by preinit
# (75_ubi_rootfs_data) and sysupgrade (platform.sh); needs nand.sh.
#
# The kernel does not attach this partition (it would attach one named
# "ubi" by itself). After an install from stock it still holds Xiaomi
# data: the stock tclinux_slave and a stock UBI image. So read the UBI EC
# header of every erase block first, and format the partition when it
# holds no UBI, more than one UBI image, or more blocks without a UBI
# header than UBI itself tolerates. Otherwise attach, and format if that
# fails. Reading all blocks takes about 2.5 s, so a normal boot reads one
# block in 16 and attaches straight away when they show a single UBI
# image and nothing else: the stock data spans hundreds of blocks.

BE5000_UBI_PART=ubi-data

be5000_ubi_log() {
	echo "be5000-ubi: $*" > /dev/kmsg 2>/dev/null
}

# Image sequence number (hex) in the EC header of erase block $2 of
# /dev/mtd$1 (erase block size $3): "-" when the block reads as erased,
# "x" when it starts with anything else.
be5000_ubi_peb_seq() {
	local h

	h="$(dd if="/dev/mtd$1" bs=2048 count=1 skip=$(( $2 * $3 / 2048 )) \
		2>/dev/null | hexdump -n 28 -v -e '28/1 "%02x"')"
	case "$h" in
	55424923*) echo "${h:48:8}" ;;
	ffffffffffffffffffffffffffffffffffffffffffffffffffffffff) echo - ;;
	*) echo x ;;
	esac
}

be5000_ubi_format() {
	be5000_ubi_log "formatting mtd$1"
	echo "- formatting UBI on mtd$1 -" >&2
	ubiformat "/dev/mtd$1" -y -q >&2 && ubiattach -m "$1" >/dev/null 2>&1
}

# Print the UBI device of the partition, attaching it first if needed.
be5000_ubi_attach() {
	local mtdnum ubidev size erasesize blocks maxjunk i seq ours=
	local junk=0 other=0

	ubidev="$(nand_find_ubi "$BE5000_UBI_PART")"
	[ -n "$ubidev" ] && { echo "$ubidev"; return 0; }

	mtdnum="$(find_mtd_index "$BE5000_UBI_PART")"
	[ -n "$mtdnum" ] || return 1
	size="$(cat "/sys/class/mtd/mtd$mtdnum/size")"
	erasesize="$(cat "/sys/class/mtd/mtd$mtdnum/erasesize")"
	blocks=$(( size / erasesize ))
	# UBI refuses to attach with this many corrupted blocks or more
	maxjunk=$(( blocks / 20 ))
	[ "$maxjunk" -ge 8 ] || maxjunk=8

	i=0
	while [ "$i" -lt "$blocks" ]; do
		seq="$(be5000_ubi_peb_seq "$mtdnum" "$i" "$erasesize")"
		case "$seq" in
		-) ;;
		x) other=1 ;;
		*)
			[ -n "$ours" ] || ours="$seq"
			[ "$seq" = "$ours" ] || other=1
			;;
		esac
		[ "$other" -eq 0 ] || break
		i=$(( i + 16 ))
	done
	if [ -n "$ours" ] && [ "$other" -eq 0 ] &&
	   ubiattach -m "$mtdnum" >/dev/null 2>&1; then
		ubidev="$(nand_find_ubi "$BE5000_UBI_PART")"
		[ -n "$ubidev" ] || return 1
		be5000_ubi_log "attached as $ubidev (sampled)"
		echo "$ubidev"
		return 0
	fi
	ours=
	other=0

	i=0
	while [ "$i" -lt "$blocks" ]; do
		seq="$(be5000_ubi_peb_seq "$mtdnum" "$i" "$erasesize")"
		case "$seq" in
		-) ;;
		x) junk=$(( junk + 1 )) ;;
		*)
			[ -n "$ours" ] || ours="$seq"
			[ "$seq" = "$ours" ] || other=$(( other + 1 ))
			;;
		esac
		i=$(( i + 1 ))
	done
	be5000_ubi_log "mtd$mtdnum: $blocks blocks, image ${ours:-none}," \
		"$junk without UBI header, $other of another UBI image"

	if [ -z "$ours" ] || [ "$other" -gt 0 ] || [ "$junk" -ge "$maxjunk" ]; then
		be5000_ubi_format "$mtdnum" || return 1
	else
		be5000_ubi_log "attaching mtd$mtdnum"
		ubiattach -m "$mtdnum" >/dev/null 2>&1 ||
			be5000_ubi_format "$mtdnum" || return 1
	fi

	ubidev="$(nand_find_ubi "$BE5000_UBI_PART")"
	[ -n "$ubidev" ] || return 1
	be5000_ubi_log "attached as $ubidev"
	echo "$ubidev"
}
