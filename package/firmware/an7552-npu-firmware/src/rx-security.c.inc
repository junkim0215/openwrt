/* SPDX-License-Identifier: MIT */
/* Mirror the driver's replay domain before separating fast and slow delivery. */
#include "pkt-view.h"
#include "pn-cache.h"
/* Replay entry word access, through the batch cache when one is given. */
#define EG(i) (pc ? pc_get(pc, (i)) : entry[i])
#define ES(i, value) do { if (pc) pc_set(pc, (i), (value)); else entry[i] = (value); } while (0)
__attribute__((noinline)) static uint32_t native_rx_security_view(struct pkt_view *v, uint32_t length,
					uint32_t *cookie, struct pn_cache *pc)
{
	uint32_t d0, d1, d2, d4, offset = 32, fc = 0, seq = 0, tid = 0, wcid,
				 low, high, result, aggregate, bad;
	volatile uint32_t *entry;
	if (length < 32)
		return 0;
	d0 = pv_get32(v, 0);
	d1 = pv_get32(v, 4);
	d2 = pv_get32(v, 8);
	d4 = pv_get32(v, 16);
	if ((d0 >> 27) != 2 || !AN7552_RX_CIPHER_IS_FAST(d2) ||
	    (d1 & ((1U << 23) | (1U << 24) | (1U << 25))) || !(d1 & (1U << 16)))
		return 0;
	wcid = d1 & 4095;
	if (wcid >= AN7552_STATION_COUNT)
		return 0;
	entry = pn_entry(wcid);
	{
		uint32_t key = entry[0];

		if (!key || read32(AN7552_NPU_BASE + PN_CONTROL + 36))
			return 0;
		if (pc)
			pc_select(pc, wcid);
		*cookie = EG(1);
		if (((d1 >> 21) & 3) != ((key >> 8) & 3))
			return (wcid << 4) | 2;
	}
	if (d1 & (1U << 19)) {
		if (length < 48)
			return 0;
		fc = pv_get32(v, 32) & 65535;
		seq = pv_get32(v, 40);
		tid = (seq >> 16) & 15;
		seq &= 65535;
		offset += 16;
	}
	if (length < offset + 16)
		return 0;
	/* Later 802.11 fragments remain under mac80211's fragment replay checks. */
	if (!(d2 & (1U << 7)) && (d2 & (1U << 27)) && (seq & 15))
		return 0;
	if (!(d2 & (1U << 7)) && !(fc & 0x0c) && !(fc & 0x100))
		tid = 16;
	low = pv_get32(v, offset);
	high = pv_byte(v, offset + 4) | (uint32_t)pv_byte(v, offset + 5) << 8;
	aggregate = d4 & 3;
	bad = 0;
	if (aggregate == 3 && (d2 & (1U << 7))) {
		uint32_t ethernet = offset + 16;
		if (d1 & (1U << 17))
			ethernet += 16;
		if (d1 & (1U << 18)) {
			ethernet += 16;
			if (d1 & (1U << 20))
				ethernet += 96;
		}
		ethernet += 2 * ((d2 >> 13) & 7);
		/* A first subframe that looks like an LLC/SNAP header: A-MSDU
		 * injection. The MPDU is dropped.
		 */
		bad = ethernet > length || length - ethernet < 14 ||
		      (pv_byte(v, ethernet) == 0xaa && pv_byte(v, ethernet + 1) == 0xaa &&
		       pv_byte(v, ethernet + 2) == 3 && !pv_byte(v, ethernet + 3) &&
		       !pv_byte(v, ethernet + 4) && !pv_byte(v, ethernet + 5));
	}
	/* A-MSDU subframes share the sequence number and PN of their MPDU.
	 * The radio does not keep them in order: MediaTek clients interleave
	 * the subframes of several MPDUs, a middle subframe can come after the
	 * last one, and a subframe for the access point itself can come before
	 * the first one. So any subframe with a new PN opens its MPDU, and the
	 * marker (0x10000 | sequence) records which subframes of it were seen:
	 * 0x40000 the first, 0x20000 the last, 0x80000 a bad first one. Further
	 * subframes of that MPDU need the same sequence number and PN; middles
	 * are taken in any order, a second first or last subframe is not, nor
	 * anything after a bad first one. A replayed first subframe closes the
	 * MPDU for all its subframes.
	 */
	{
		uint32_t old_low = EG(4 + tid * 2), old_high = EG(5 + tid * 2);
		uint32_t old_marker = EG(40 + tid), marker = old_marker;
		uint32_t seen = aggregate == 3 ? 0x40000 :
				aggregate == 1 ? 0x20000 : 0;

		if (aggregate && (marker & 0x10000) &&
		    (marker & 0xfff) == (seq >> 4) &&
		    low == old_low && high == old_high) {
			result = !bad && !(marker & (seen | 0x80000));
			if (result)
				marker |= seen;
			else if (bad)
				marker |= 0x80000;
			else if (aggregate == 3)
				marker = 0;
		} else if (high > old_high ||
			   (high == old_high && low > old_low)) {
			ES(4 + tid * 2, low);
			ES(5 + tid * 2, high);
			result = !bad;
			marker = aggregate ? 0x10000 | (seq >> 4) | seen |
						     (bad ? 0x80000 : 0) :
					     0;
		} else {
			result = 0;
			if (aggregate == 3)
				/* A replayed MPDU: none of its subframes may follow. */
				marker = 0;
		}
		if (marker != old_marker)
			ES(40 + tid, marker);
	}
	*cookie = EG(1);
	return (wcid << 4) | (result == 1 ? 1 : 2);
}

__attribute__((unused)) static uint32_t
native_rx_security(volatile uint8_t *packet, uint32_t length, uint32_t *cookie)
{
	struct pkt_view v;

	pv_init(&v, packet, length);
	return native_rx_security_view(&v, length, cookie, 0);
}
