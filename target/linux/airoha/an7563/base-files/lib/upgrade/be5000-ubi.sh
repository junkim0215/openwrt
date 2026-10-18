# SPDX-License-Identifier: GPL-2.0-only

# Attach the "ubi-data" partition of the Xiaomi BE5000, shared by preinit
# (75_ubi_rootfs_data) and sysupgrade (platform.sh); needs nand.sh.
#
# The kernel does not attach this partition (it would attach one named
# "ubi" by itself): up to the 2026-10-17 image it was the 40 MiB "ubi", and
# on a router upgraded from there the new part still holds Xiaomi data,
# the stock tclinux_slave and a stock UBI image among it. So read the UBI
# EC header of every erase block first. "Ours" is the image sequence number
# of the first UBI block in the old 40 MiB.
#  - No UBI of ours, another UBI image in the old 40 MiB, or more blocks
#    without a UBI header there than UBI itself tolerates: the partition is
#    not ours (first boot after an install from stock), format it.
#  - None of our blocks in the new part yet: this is the first boot after
#    the upgrade, erase the new part as a whole.
#  - Our blocks in the new part: erase only blocks of another UBI image
#    there, one by one, and leave anything else to UBI.
# Then attach, and format if that fails.

BE5000_UBI_PART=ubi-data
BE5000_UBI_OLD_SIZE=$((0x2800000))

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
	local mtdnum ubidev size erasesize blocks old maxjunk i seq ours=
	local junk_head=0 other_head=0 junk_tail=0 other_tail=0 ours_tail=0
	local other_list=

	ubidev="$(nand_find_ubi "$BE5000_UBI_PART")"
	[ -n "$ubidev" ] && { echo "$ubidev"; return 0; }

	mtdnum="$(find_mtd_index "$BE5000_UBI_PART")"
	[ -n "$mtdnum" ] || return 1
	size="$(cat "/sys/class/mtd/mtd$mtdnum/size")"
	erasesize="$(cat "/sys/class/mtd/mtd$mtdnum/erasesize")"
	blocks=$(( size / erasesize ))
	old=$(( BE5000_UBI_OLD_SIZE / erasesize ))
	[ "$old" -le "$blocks" ] || old=$blocks
	# UBI refuses to attach with this many corrupted blocks or more
	maxjunk=$(( blocks / 20 ))
	[ "$maxjunk" -ge 8 ] || maxjunk=8

	i=0
	while [ "$i" -lt "$blocks" ]; do
		seq="$(be5000_ubi_peb_seq "$mtdnum" "$i" "$erasesize")"
		if [ "$i" -lt "$old" ]; then
			case "$seq" in
			-) ;;
			x) junk_head=$(( junk_head + 1 )) ;;
			*)
				[ -n "$ours" ] || ours="$seq"
				[ "$seq" = "$ours" ] || other_head=$(( other_head + 1 ))
				;;
			esac
		else
			case "$seq" in
			-) ;;
			x) junk_tail=$(( junk_tail + 1 )) ;;
			"$ours") ours_tail=$(( ours_tail + 1 )) ;;
			*)
				other_tail=$(( other_tail + 1 ))
				other_list="$other_list $i"
				;;
			esac
		fi
		i=$(( i + 1 ))
	done
	be5000_ubi_log "mtd$mtdnum: $blocks blocks, image ${ours:-none};" \
		"old part: $junk_head without UBI header, $other_head of another UBI;" \
		"new part: $ours_tail ours, $junk_tail without UBI header, $other_tail of another UBI"

	if [ -z "$ours" ] || [ "$other_head" -gt 0 ] ||
	   [ "$junk_head" -ge "$maxjunk" ]; then
		be5000_ubi_format "$mtdnum" || return 1
	else
		if [ "$ours_tail" -eq 0 ] &&
		   [ $(( junk_tail + other_tail )) -gt 0 ]; then
			be5000_ubi_log "erasing mtd$mtdnum behind the old 40 MiB"
			echo "- erasing mtd$mtdnum behind the old 40 MiB -" >&2
			flash_erase -q "/dev/mtd$mtdnum" $(( old * erasesize )) \
				$(( blocks - old )) >&2 || return 1
		else
			for i in $other_list; do
				be5000_ubi_log "erasing block $i of another UBI image"
				flash_erase -q "/dev/mtd$mtdnum" $(( i * erasesize )) 1 >&2
			done
		fi
		be5000_ubi_log "attaching mtd$mtdnum"
		ubiattach -m "$mtdnum" >/dev/null 2>&1 ||
			be5000_ubi_format "$mtdnum" || return 1
	fi

	ubidev="$(nand_find_ubi "$BE5000_UBI_PART")"
	[ -n "$ubidev" ] || return 1
	be5000_ubi_log "attached as $ubidev"
	echo "$ubidev"
}
