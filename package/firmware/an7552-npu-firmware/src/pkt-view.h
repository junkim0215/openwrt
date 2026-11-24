/* SPDX-License-Identifier: MIT */
#ifndef AN7552_PKT_VIEW_H
#define AN7552_PKT_VIEW_H
#include <stdint.h>

/* Packet buffers are uncached (~80 cycles per load) while the stack is
 * cached (~10). Load each header word at most once per frame.
 */
#define PKT_VIEW_WORDS 96U

/* Each NPU core has its own data cache (Rocket, 64-byte lines) and no
 * coherence with the radio DMA. CDISCARD.D.L1 / CFLUSH.D.L1 (Rocket custom
 * instructions, address in rs1, x0 for the whole cache) drop one line, at
 * 12-17 cycles plus pipeline cost.
 */
#define L1_LINE 64U
#ifdef __riscv
static inline void l1_discard(uintptr_t address)
{
	__asm__ volatile("mv a0, %0\n\t.word 0xfc250073"	/* cdiscard.d.l1 a0 */
			 : : "r"(address) : "a0", "memory");
}

static inline void l1_flush_all(void)
{
	__asm__ volatile(".word 0xfc000073" : : : "memory"); /* cflush.d.l1 x0 */
}
#else
static inline void l1_discard(uintptr_t address) { (void)address; }
static inline void l1_flush_all(void) { }
#endif

static inline void l1_discard_range(const volatile void *base, uint32_t bytes)
{
	uintptr_t start = (uintptr_t)base & ~(uintptr_t)(L1_LINE - 1),
		  end = (uintptr_t)base + bytes;

	for (uintptr_t a = start; a < end; a += L1_LINE)
		l1_discard(a);
}
struct pkt_view {
	const volatile uint8_t *base;
	uint32_t length;
	uint32_t top;		/* end of the words loaded, in words */
	uint32_t valid[PKT_VIEW_WORDS / 32];
	uint32_t word[PKT_VIEW_WORDS];
};

static inline void pv_init(struct pkt_view *v, const volatile uint8_t *base,
			   uint32_t length)
{
	v->base = base;
	v->length = length;
	v->top = 0;
	for (uint32_t i = 0; i < PKT_VIEW_WORDS / 32; i++)
		v->valid[i] = 0;
}

/* Never load past the frame: the tail word is assembled bytewise. */
static inline uint32_t pv_load(struct pkt_view *v, uint32_t index)
{
	uint32_t offset = index * 4, value = 0;

	if (index >= v->top)
		v->top = index + 1;
	if (offset + 4 <= v->length && !((uintptr_t)v->base & 3))
		return *(const volatile uint32_t *)(v->base + offset);
	for (uint32_t i = 0; i < 4 && offset + i < v->length; i++)
		value |= (uint32_t)v->base[offset + i] << (i * 8);
	return value;
}

static inline uint32_t pv_word(struct pkt_view *v, uint32_t index)
{
	uint32_t bit = 1U << (index & 31);

	if (index >= PKT_VIEW_WORDS)
		return pv_load(v, index);
	if (!(v->valid[index >> 5] & bit)) {
		v->word[index] = pv_load(v, index);
		v->valid[index >> 5] |= bit;
	}
	return v->word[index];
}

static inline uint32_t pv_byte(struct pkt_view *v, uint32_t offset)
{
	return (pv_word(v, offset >> 2) >> ((offset & 3) * 8)) & 0xff;
}

__attribute__((noinline)) static uint32_t pv_get32(struct pkt_view *v, uint32_t offset)
{
	uint32_t shift = (offset & 3) * 8;

	if (!shift)
		return pv_word(v, offset >> 2);
	return (pv_word(v, offset >> 2) >> shift) |
	       (pv_word(v, (offset >> 2) + 1) << (32 - shift));
}
#endif
