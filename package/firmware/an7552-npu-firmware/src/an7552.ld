/* SPDX-License-Identifier: MIT */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
	CODE (rx) : ORIGIN = 0x84000000, LENGTH = 0x10000
	LOCAL (rw) : ORIGIN = 0x3e900000, LENGTH = 0x3000
	STACK (rw) : ORIGIN = 0x84100000, LENGTH = 0x4000
}
SECTIONS {
	.fw_header : { __image_start = .; KEEP(*(.fw_header)) } > CODE
	.init : { KEEP(*(.init)) } > CODE
	/* Hot code of each core contiguous: core 0 first, then core 1, the
	 * rest (boot, trap, park) last.
	 */
	.text : {
		*(.text.core0_loop*)
		*(.text.native_txfree*) *(.text.txfree_walk*) *(.text.tx_pool_publish*)
		*(.text.pn_service_run*)
		*(.text.native_up_front*) *(.text.up_front_one*)
		*(.text.native_rx_security_view*) *(.text.rx_parse_view*)
		*(.text.pv_get32*) *(.text.pv_byte*) *(.text.pv_word*)
		*(.text.native_transmit*) *(.text.native_download*)
		*(.text.native_forward_ring*) *(.text.tx_pool_arm*)
		*(.text.core1_loop*)
		*(.text.native_ppe_service*) *(.text.native_ppe_pop*)
		*(.text.native_up_back*) *(.text.up_back_one*) *(.text.up_back_flush*)
		*(.text.native_ppe_queue*) *(.text.rx_deliver_words*)
		*(.text.native_rx_refill*) *(.text.native_rx_release*)
		*(.text.pn_compare*) *(.text.st_load*) *(.text.up_quiet*)
		*(.text .text.*)
	} > CODE
	.rodata : { *(.rodata .rodata.*) } > CODE
	__image_size = . - __image_start;
	__entry_offset = _start - __image_start;
	.data : { *(.data .data.* .sdata .sdata.*) } > LOCAL
	.bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* COMMON) } > LOCAL
	.stacks (NOLOAD) : { __stack_base = .; . += 0x4000; } > STACK
	/DISCARD/ : { *(.comment .note.* .eh_frame*) }
}
/* The firmware intentionally has no runtime-initialized global state. */
ASSERT(SIZEOF(.data) == 0, "unexpected initialized data")
ASSERT(SIZEOF(.bss) == 0, "unexpected BSS")

ASSERT(SIZEOF(.fw_header) == 32, "invalid firmware header size")
