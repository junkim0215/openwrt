/* SPDX-License-Identifier: MIT */
/* Block-ack receive reordering on core 0, before replay validation.
 *
 * The MT7992 RRO marks frames (repeat, behind its window, flushed) but hands
 * them to the RX completion ring in air order. A client sending one TID to
 * several destination addresses transmits interleaved sequence ranges, and
 * some clients (MediaTek) also interleave the A-MSDU subframes of three or
 * four MPDUs and deliver a middle subframe after the last one. Validating in
 * arrival order rejects those frames as replays.
 *
 * Most clients send in sequence, so reordering starts for a station and TID
 * only after RO_TRIGGER replay rejections at least RO_TRIGGER_GAP apart
 * within RO_TRIGGER_WINDOW, and
 * stops after RO_QUIET without a frame out of sequence. Without a session a
 * frame costs one load here.
 *
 * In sequence and with nothing held, a frame goes straight on: a single
 * MSDU, or the subframes of one A-MSDU once the previous A-MSDU is closed by
 * its last subframe. Otherwise core 0 keeps the descriptor in a pool (the RX
 * buffer stays in receive state 1, owned by the firmware). Each sequence
 * number collects its subframes sorted first, middle..., last; it is complete
 * with a single MSDU or with both its first and last subframe. Complete
 * MPDUs leave in sequence order, whole, when the head reaches them; a gap or
 * an incomplete MPDU at the head is given up after a timeout, and a frame
 * far ahead of the window or a full pool flushes what is held.
 * Held frames are not validated yet: they behave like frames still queued in
 * the radio ring, so station and key changes need no special handling.
 * Replay validation stays strict and still decides about every frame.
 * At an RRO stop the pool is dropped together with the record queue; Linux
 * re-initialises every buffer in receive state 1 before the next start.
 *
 * Based on the analysis and the reorder pool of Paulo José Rolim Braga Filho
 * (paulojjj), issue #11 of pabragin/xiaomi-be5000-openwrt.
 */
#define RO_SESSIONS 8U
#ifndef RO_WINDOW
#define RO_WINDOW 512U		/* sequence numbers per session */
#endif
#ifndef RO_POOL
#define RO_POOL 1536U		/* held descriptors, all sessions */
#endif
/* NPU core cycles (mcycle, ~686 MHz on the AN7563). */
#ifndef RO_TIMEOUT
#define RO_TIMEOUT 13720000U	/* gap timeout, about 20 ms */
#endif
/* Subframes of an open A-MSDU follow within microseconds. */
#define RO_OPEN_TIMEOUT 686000U	/* an open A-MSDU closes after about 1 ms */
#define RO_IDLE 68600000U	/* sequence restart after about 100 ms idle */
#define RO_TRIGGER 3U		/* replay rejections that start a session */
#define RO_TRIGGER_WINDOW 34300000U	/* ... within about 50 ms */
#define RO_TRIGGER_GAP 1372000U	/* ... counted at most once in about 2 ms */
#define RO_QUIET 686000000U	/* a session ends after about 1 s in order */
#define RO_STATE_SRAM 0x3e901820U
#define RO_SN_MASK 4095U
/* DRAM (uncached): pool entries, then per-session slot lists. */
#define RO_POOL_NPU AN7552_REORDER_NPU
#define RO_SLOT_NPU (AN7552_REORDER_NPU + RO_POOL * 16)

struct ro_desc {
	uint32_t d0, d1, d2, d3;
	uint32_t split;		/* part of a multi-buffer MPDU: never reordered */
	uint32_t cont;		/* continues the previous buffer's MPDU */
	uint32_t accept;	/* in sequence despite an RRO repeat/old mark */
};

struct ro_session {
	uint16_t key;		/* wcid << 4 | tid */
	uint16_t head;		/* next sequence number to pass on */
	uint16_t flush_end;	/* flushing: pass on everything before this SN */
	uint16_t held;		/* occupied sequence numbers */
	uint8_t in_use, ready, flushing, open;	/* open: an A-MSDU passed
						 * straight on, last subframe
						 * not seen yet */
	uint8_t fresh, pad[3];	/* fresh: the next frame sets the head */
	uint32_t since;		/* cycle count when the head started waiting */
	uint32_t open_since;	/* cycle count when the open A-MSDU began */
	uint32_t used;
	uint32_t cookie;	/* replay entry word 1 when the session started */
	uint32_t seen;		/* cycle count of the last frame */
	uint32_t unordered;	/* cycle count of the last frame held */
	uint32_t map[RO_WINDOW / 32]; /* occupied slots */
};

struct ro_state {
	uint32_t free_count, deferred, stamp;
	uint32_t busy;		/* sessions that hold frames or are ready */
	uint32_t active;	/* sessions in use */
	uint32_t rejects, reject_since;	/* replay rejections, trigger window */
	uint32_t reject_last;	/* cycle count of the last rejection counted */
	uint32_t last;		/* session of the previous frame */
	uint32_t now;		/* cycle count read for the current frame */
	struct ro_desc pending;
	uint16_t free_stack[RO_POOL];
	struct ro_session s[RO_SESSIONS];
};

_Static_assert(RO_STATE_SRAM >= RX_STATE_SRAM + AN7552_RX_BUFFER_COUNT + 8,
	       "reorder state follows the receive states");
_Static_assert(RO_STATE_SRAM + sizeof(struct ro_state) <= 0x3e902800U,
	       "reorder state fits below the record queue");
_Static_assert(RO_POOL * 16 + RO_SESSIONS * RO_WINDOW * 2 <=
		       AN7552_REORDER_SIZE,
	       "reorder pool fits its reservation");
_Static_assert(RO_POOL < 32767 && RO_WINDOW <= 2048 && !(RO_WINDOW % 32),
	       "reorder limits");

/* Statistics, NPU control words 0x7a0..0x7df (unused by Linux). */
#define RO_STAT ((volatile uint32_t *)(uintptr_t)(AN7552_NPU_BASE + 0x7a0))
enum {
	RO_ST_HELD, RO_ST_START, RO_ST_TIMEOUT, RO_ST_OLD, RO_ST_JUMP,
	RO_ST_POOL_FULL, RO_ST_NO_SESSION, RO_ST_DUP, RO_ST_REPEAT,
	RO_ST_OLDPKT, RO_ST_PEAK, RO_ST_PARTIAL, RO_ST_RESYNC,
	RO_ST_FILLED, RO_ST_STOP, RO_ST_RESET
};

static inline struct ro_state *ro(void)
{
	return (struct ro_state *)(uintptr_t)RO_STATE_SRAM;
}

/* Keep the busy mask in step with a session's held count and ready flag:
 * the receive path skips the reorder work while it is zero.
 */
static inline void ro_mark(struct ro_session *s)
{
	struct ro_state *r = ro();
	uint32_t bit = 1U << (s - r->s);

	if (s->ready || s->held)
		r->busy |= bit;
	else
		r->busy &= ~bit;
}

#ifdef __riscv
static inline uint32_t ro_now(void)
{
	uint32_t now;

	__asm__ volatile("csrr %0, mcycle" : "=r"(now));
	return now;
}
#else
/* Host tests: a clock that moves 1 ms per reading. */
static uint32_t ro_test_clock;
static inline uint32_t ro_now(void)
{
	return ro_test_clock += 686000U;
}
#endif

/* Entry words: d1, d2, d3, next entry + 1 | A-MSDU position << 16 |
 * accept << 24.
 */
static inline volatile uint32_t *ro_entry(uint32_t entry)
{
	return (volatile uint32_t *)(uintptr_t)(RO_POOL_NPU + entry * 16);
}

static inline volatile uint16_t *ro_slot(struct ro_session *s, uint32_t sn)
{
	uint32_t index = s - ro()->s;

	return (volatile uint16_t *)(uintptr_t)RO_SLOT_NPU +
	       index * RO_WINDOW + sn % RO_WINDOW;
}

static inline uint32_t ro_occupied(struct ro_session *s, uint32_t sn)
{
	uint32_t pos = sn % RO_WINDOW;

	return (s->map[pos / 32] >> (pos % 32)) & 1;
}

static void ro_reset(void)
{
	struct ro_state *r = ro();

	for (uint32_t i = 0; i < RO_POOL; i++)
		r->free_stack[i] = i;
	r->free_count = RO_POOL;
	r->deferred = 0;
	r->stamp = 0;
	r->busy = 0;
	r->active = 0;
	r->rejects = 0;
	r->last = 0;
	for (uint32_t i = 0; i < RO_SESSIONS; i++) {
		struct ro_session *s = &r->s[i];

		s->in_use = s->ready = s->flushing = s->open = s->fresh = 0;
		s->held = 0;
		for (uint32_t j = 0; j < RO_WINDOW / 32; j++)
			s->map[j] = 0;
	}
	RO_STAT[RO_ST_RESET]++;
}

static inline uint32_t ro_marked(const struct ro_desc *d)
{
	uint32_t reason = (d->d2 >> 12) & 15;

	return reason == 1 || reason == 2;
}

/* The host drops frames the RRO marked repeat (1) or old (2). A marked
 * frame that is in sequence here is validated like any other; its mark is
 * cleared only if the replay check then accepts it.
 */
static inline void ro_accept(struct ro_desc *d)
{
	d->accept = ro_marked(d);
}

/* Reorder key of a single-buffer, translated, unicast QoS data MPDU. */
static int ro_classify(struct pkt_view *view, const struct ro_desc *d,
		       uint32_t *key, uint32_t *sn, uint32_t *agg)
{
	uint32_t w0, w1, w2, fc, seq, tid;

	if (d->split || view->length < 64)
		return 0;
	w0 = pv_get32(view, 0);
	w1 = pv_get32(view, 4);
	w2 = pv_get32(view, 8);
	if ((w0 >> 27) != 2 || (w0 & 65535) != view->length ||
	    !(w1 & (1U << 16)) || !(w1 & (1U << 19)) ||
	    (w1 & (7U << 23)) || ((w1 >> 27) & 3) >= 2 ||
	    (w1 & 4095) >= AN7552_STATION_COUNT || !(w2 & (1U << 7)) ||
	    (w2 & (1U << 27)))
		return 0;
	fc = pv_get32(view, 32) & 65535;
	seq = pv_get32(view, 40);
	tid = (seq >> 16) & 15;
	if ((fc & 0x8c) != 0x88 || (fc & 0x0400) || (seq & 15) || tid >= 8)
		return 0;
	*key = (w1 & 4095) << 4 | tid;
	*sn = (seq & 65535) >> 4;
	*agg = pv_get32(view, 16) & 3;
	return 1;
}

static struct ro_session *ro_lookup(uint32_t key)
{
	struct ro_state *r = ro();

	for (uint32_t i = 0, j = r->last; i < RO_SESSIONS;
	     i++, j = (j + 1) % RO_SESSIONS) {
		struct ro_session *s = &r->s[j];

		if ((r->active >> j) & 1 && s->key == key)
			return s;
	}
	return 0;
}

/* The session of a frame, if reordering runs for its station and TID. */
static struct ro_session *ro_session(uint32_t key, uint32_t sn)
{
	struct ro_state *r = ro();
	struct ro_session *s = ro_lookup(key);
	uint32_t j, delta, now;

	if (!s)
		return 0;
	j = s - r->s;
	now = ro_now();
	r->now = now;
	/* Usually the same session as the previous frame. */
	if (j != r->last) {
		r->last = j;
		s->used = ++r->stamp;
	}
	delta = (sn - s->head) & RO_SN_MASK;
	/* A new key or a long pause can restart the sequence. The replay
	 * entry (uncached DDR) is read only for a frame outside the window: a
	 * new key restarts the numbers.
	 */
	if (s->fresh ||
	    (!s->held && !s->ready &&
	     (now - s->seen > RO_IDLE ||
	      (delta >= RO_WINDOW && delta != RO_SN_MASK &&
	       s->cookie != pn_entry(key >> 4)[1])))) {
		if (!s->fresh)
			RO_STAT[RO_ST_RESYNC]++;
		s->fresh = 0;
		s->head = sn;
		s->open = 0;
		s->cookie = pn_entry(key >> 4)[1];
	}
	s->seen = now;
	return s;
}

/* Start reordering for a station and TID; the next frame sets the head. */
static void ro_start(uint32_t key, uint32_t now)
{
	struct ro_state *r = ro();
	struct ro_session *s, *victim = 0;

	for (uint32_t i = 0; i < RO_SESSIONS; i++) {
		s = &r->s[i];
		if (!s->in_use) {
			victim = s;
			break;
		}
		if (s->held || s->ready)
			continue;
		if (!victim || (int32_t)(s->used - victim->used) < 0)
			victim = s;
	}
	if (!victim)
		return;
	victim->in_use = 1;
	victim->key = key;
	victim->held = 0;
	victim->ready = victim->flushing = victim->open = 0;
	victim->fresh = 1;
	victim->used = ++r->stamp;
	victim->seen = victim->unordered = now;
	r->active |= 1U << (victim - r->s);
	RO_STAT[RO_ST_START]++;
}

/* The replay check rejected a frame the RRO did not mark: after
 * RO_TRIGGER of them spread over a short time (lasting disorder, not one
 * burst), reorder that station and TID.
 */
static void ro_rejected(struct pkt_view *view, const struct ro_desc *d)
{
	struct ro_state *r = ro();
	uint32_t key, sn, agg, now;

	if (d->cont || ro_marked(d) || !ro_classify(view, d, &key, &sn, &agg) ||
	    ro_lookup(key))
		return;
	now = ro_now();
	if (!r->rejects || now - r->reject_since > RO_TRIGGER_WINDOW) {
		r->rejects = 0;
		r->reject_since = now;
	} else if (now - r->reject_last < RO_TRIGGER_GAP) {
		/* One burst counts once: a client that sends in sequence
		 * loses a few frames at a rare disorder, while a session
		 * would cost it a third of core 0 for every frame.
		 */
		return;
	}
	r->reject_last = now;
	if (++r->rejects >= RO_TRIGGER) {
		r->rejects = 0;
		ro_start(key, now);
	}
}

/* Slot word: first pool entry + 1 (bits 0..14), MPDU complete (bit 15). An
 * MPDU is complete once its last A-MSDU subframe (or its only MSDU) is held.
 */
#define RO_COMPLETE 0x8000U
#define RO_ENTRY_MASK 0x7fffU

/* Pass on everything held before end, complete or not, skipping gaps. */
static void ro_flush(struct ro_session *s, uint32_t end)
{
	s->flush_end = end & RO_SN_MASK;
	s->flushing = 1;
	s->ready = 1;
	ro_mark(s);
}

static inline uint32_t ro_in_flush(struct ro_session *s)
{
	return s->flushing &&
	       ((s->flush_end - s->head) & RO_SN_MASK) - 1U < 2047U;
}

static uint32_t ro_first_held(struct ro_session *s)
{
	for (uint32_t n = 0; n < RO_WINDOW;) {
		uint32_t pos = (s->head + n) % RO_WINDOW;
		uint32_t bits = s->map[pos / 32] >> (pos % 32);

		if (bits) {
			while (!(bits & 1)) {
				bits >>= 1;
				n++;
			}
			return (s->head + n) & RO_SN_MASK;
		}
		n += 32 - pos % 32;
	}
	return s->head;
}

static inline uint32_t ro_head_complete(struct ro_session *s)
{
	return ro_occupied(s, s->head) && (*ro_slot(s, s->head) & RO_COMPLETE);
}

static void ro_tick(void)
{
	struct ro_state *r = ro();
	uint32_t now;

	if (!r->active)
		return;
	now = ro_now();

	for (uint32_t i = 0; i < RO_SESSIONS; i++) {
		struct ro_session *s = &r->s[i];

		if (!s->in_use)
			continue;
		if (!s->held && !s->ready && now - s->unordered > RO_QUIET) {
			/* In sequence for a while: back to the direct path. */
			s->in_use = 0;
			r->active &= ~(1U << i);
			RO_STAT[RO_ST_STOP]++;
			continue;
		}
		if (s->held && !s->ready && now - s->since > RO_TIMEOUT) {
			/* Give up on the oldest gap or incomplete MPDU only. */
			ro_flush(s, ro_first_held(s) + 1);
			RO_STAT[RO_ST_TIMEOUT]++;
		}
	}
}

/* Next held descriptor that may go on now, if any. The subframes of one
 * MPDU leave one after another: nothing of the same session in between.
 */
static int ro_release(struct ro_desc *out)
{
	struct ro_state *r = ro();

	if (!r->busy)
		return 0;

	for (uint32_t i = 0; i < RO_SESSIONS; i++) {
		struct ro_session *s = &r->s[i];

		if (!s->ready)
			continue;
		for (uint32_t step = 0; step <= RO_WINDOW; step++) {
			uint32_t flush = ro_in_flush(s);

			if (ro_occupied(s, s->head)) {
				volatile uint16_t *slot = ro_slot(s, s->head);
				uint32_t v = *slot, entry, meta, next;
				volatile uint32_t *e;

				if (!(v & RO_COMPLETE) && !flush)
					break;
				if (!(v & RO_COMPLETE))
					RO_STAT[RO_ST_PARTIAL]++;
				entry = (v & RO_ENTRY_MASK) - 1;
				e = ro_entry(entry);
				out->d1 = e[0];
				out->d2 = e[1];
				out->d3 = e[2];
				meta = e[3];
				out->d0 = AN7552_RX_BUFFER_HOST +
					  ((out->d2 >> 16) - AN7552_RX_TOKEN_BASE) * 2048;
				out->split = 0;
				out->cont = 0;
				out->accept = (meta >> 24) & 1;
				next = meta & 65535;
				r->free_stack[r->free_count++] = entry;
				if (next) {
					*slot = (v & RO_COMPLETE) | next;
				} else {
					uint32_t pos = s->head % RO_WINDOW;

					*slot = 0;
					s->map[pos / 32] &= ~(1U << (pos % 32));
					s->held--;
					ro_mark(s);
					s->head = (s->head + 1) & RO_SN_MASK;
				}
				return 1;
			}
			if (flush) {
				s->head = s->held ? (s->head + 1) & RO_SN_MASK :
						    s->flush_end;
				continue;
			}
			break;
		}
		s->flushing = 0;
		s->ready = 0;
		ro_mark(s);
		if (s->held)
			s->since = ro_now();
	}
	return 0;
}

#define RO_EMIT 0
#define RO_HOLD 1
#define RO_DEFER 2

/* Position of an A-MSDU subframe: first (3) or single (0), middle (2),
 * last (1).
 */
static inline uint32_t ro_rank(uint32_t agg)
{
	return agg == 2 ? 1 : agg == 1 ? 2 : 0;
}

static int ro_hold(struct ro_session *s, struct ro_desc *d,
		   uint32_t sn, uint32_t agg)
{
	struct ro_state *r = ro();
	volatile uint16_t *slot = ro_slot(s, sn);
	uint32_t pos = sn % RO_WINDOW, occupied = ro_occupied(s, sn), entry;

	if (!r->free_count) {
		/* Free the oldest gap of every session that holds frames. */
		RO_STAT[RO_ST_POOL_FULL]++;
		for (uint32_t i = 0; i < RO_SESSIONS; i++)
			if (r->s[i].held)
				ro_flush(&r->s[i], ro_first_held(&r->s[i]) + 1);
		r->pending = *d;
		r->deferred = 1;
		return RO_DEFER;
	}
	ro_accept(d);
	s->unordered = r->now;
	entry = r->free_stack[--r->free_count];
	{
		volatile uint32_t *e = ro_entry(entry);

		e[0] = d->d1;
		e[1] = d->d2;
		e[2] = d->d3;
		e[3] = d->accept << 24 | agg << 16;
	}
	if (!occupied) {
		*slot = entry + 1;
		s->map[pos / 32] |= 1U << (pos % 32);
		if (!s->held++)
			s->since = ro_now();
		ro_mark(s);
	} else {
		uint32_t first = (*slot & RO_ENTRY_MASK) - 1, rank = ro_rank(agg),
			 prev, next;

		/* A second MPDU with this sequence number (a retry, or a stale
		 * buffer the RRO marked old) joins the first one: replay
		 * validation accepts at most one copy of each subframe.
		 */
		if ((*slot & RO_COMPLETE) && (agg == 0 || agg == 3))
			RO_STAT[RO_ST_DUP]++;
		/* The radio can deliver the subframes of one A-MSDU out of
		 * order. Replay validation needs first, middle..., last: keep
		 * the chain sorted by position, arrival order within a rank.
		 */
		if (ro_rank(ro_entry(first)[3] >> 16 & 255) > rank) {
			ro_entry(entry)[3] |= first + 1;
			*slot = (*slot & RO_COMPLETE) | (entry + 1);
		} else {
			prev = first;
			while ((next = ro_entry(prev)[3] & 65535) &&
			       ro_rank(ro_entry(next - 1)[3] >> 16 & 255) <= rank)
				prev = next - 1;
			ro_entry(entry)[3] |= next;
			ro_entry(prev)[3] = (ro_entry(prev)[3] & ~65535U) |
					    (entry + 1);
		}
	}
	/* Complete: a single MSDU, or both the first and the last subframe. */
	{
		uint32_t at = (*slot & RO_ENTRY_MASK), seen = 0;

		while (at) {
			seen |= 1U << (ro_entry(at - 1)[3] >> 16 & 3);
			at = ro_entry(at - 1)[3] & 65535;
		}
		if ((seen & 1) || (seen & 0xa) == 0xa)
			*slot |= RO_COMPLETE;
	}
	if (sn == s->head && (*slot & RO_COMPLETE)) {
		s->ready = 1;
		ro_mark(s);
	}
	RO_STAT[RO_ST_HELD]++;
	if (RO_POOL - r->free_count > RO_STAT[RO_ST_PEAK])
		RO_STAT[RO_ST_PEAK] = RO_POOL - r->free_count;
	return RO_HOLD;
}

/* Decide what to do with a descriptor just taken from the ring. */
static int ro_admit(struct pkt_view *view, struct ro_desc *d)
{
	struct ro_state *r = ro();
	struct ro_session *s;
	uint32_t key, sn, agg, delta;

	if (!r->active || !ro_classify(view, d, &key, &sn, &agg))
		return RO_EMIT;
	s = ro_session(key, sn);
	if (!s)
		return RO_EMIT;
	if (ro_marked(d))
		RO_STAT[(d->d2 >> 12 & 15) == 1 ? RO_ST_REPEAT : RO_ST_OLDPKT]++;
	delta = (sn - s->head) & RO_SN_MASK;
	if (s->open && r->now - s->open_since > RO_OPEN_TIMEOUT)
		s->open = 0;
	/* The rest of an A-MSDU that went straight on follows it. */
	if (s->open && delta == RO_SN_MASK && (agg == 2 || agg == 1)) {
		if (agg == 1)
			s->open = 0;
		return RO_EMIT;
	}
	if (delta >= 2048) {
		/* Already passed on: late frame, replay validation decides. */
		RO_STAT[RO_ST_OLD]++;
		return RO_EMIT;
	}
	if (delta >= RO_WINDOW) {
		if (ro_marked(d)) {
			/* The RRO also releases long-stale buffers marked old:
			 * they must never move the window.
			 */
			RO_STAT[RO_ST_OLD]++;
			return RO_EMIT;
		}
		/* Far ahead: pass on everything held, restart the window. */
		RO_STAT[RO_ST_JUMP]++;
		if (s->held) {
			ro_flush(s, sn);
			r->pending = *d;
			r->deferred = 1;
			return RO_DEFER;
		}
		s->head = sn;
		delta = 0;
	}
	/* In sequence with nothing held: a single MSDU, or the first subframe
	 * of an A-MSDU once the previous one is closed, goes straight on.
	 * Interleaved A-MSDUs take the pool.
	 */
	if (!delta && !s->held && !s->ready && agg == 3 && !s->open) {
		ro_accept(d);
		s->head = (sn + 1) & RO_SN_MASK;
		s->open = 1;
		s->open_since = r->now;
		return RO_EMIT;
	}
	if (!delta && agg == 0 && !ro_occupied(s, sn) && !s->open) {
		if (s->held)
			RO_STAT[RO_ST_FILLED]++;
		ro_accept(d);
		s->head = (sn + 1) & RO_SN_MASK;
		if (ro_head_complete(s)) {
			s->ready = 1;
			ro_mark(s);
		}
		return RO_EMIT;
	}
	return ro_hold(s, d, sn, agg);
}
