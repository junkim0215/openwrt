/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include "protocol.h"
#include "firmware-abi.h"

static inline void barrier(void)
{
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}

static uint32_t read32(uint32_t address)
{
	return *(volatile uint32_t *)(uintptr_t)address;
}

#include "tdma.c.inc"
#include "station.c.inc"
#include "tx-pool.c.inc"
#include "native-tx.c.inc"
#include "txfree.c.inc"
#include "pn.c.inc"
#include "rx-security.c.inc"
#include "rx-parse.h"
#include "rro.c.inc"
#include "ppe.c.inc"
#include "upsplit.c.inc"
#include "prof.h"

/* Park request from Linux: acknowledge and stop for good. */
static void park_check(uint32_t core)
{
	if (!read32(AN7552_NPU_BASE + AN7552_PARK_CONTROL))
		return;
	barrier();
	*(volatile uint32_t *)(uintptr_t)(AN7552_NPU_BASE + AN7552_PARK_ACK +
					  core * 4) = AN7552_PARK_MAGIC;
	barrier();
	for (;;)
		__asm__ volatile("wfi");
}

/* Station quiesce handshake: acknowledge at word 'ack' (4: core 0, 8: core
 * 1) and return nonzero while Linux holds the station table.
 */
static uint32_t station_quiesced(uint32_t ack)
{
	volatile uint32_t *control = (void *)(uintptr_t)(AN7552_NPU_BASE +
							  AN7552_STATION_CONTROL);
	uint32_t busy = control[0] != 0;

	if (busy)
		barrier();
	control[ack / 4] = busy;
	return busy;
}

/* Core 1: back half of the Wi-Fi -> Ethernet direction (buffer ownership,
 * PPE input, delivery to Linux, refill) and PPE return pops.
 */
__attribute__((noinline, noreturn)) static void core1_loop(volatile uint32_t *state)
{
	uint32_t heartbeat = 0;

	PROF_DECLARE(prof, 1);
	for (;;) {
		PROF_TICK(prof, 1);
		park_check(1);
		state[1] = ++heartbeat;
		PROF(prof, 2, native_ppe_service());
		if (station_quiesced(8))
			continue;
		PROF(prof, 3, native_up_back());
	}
}

/* Core 0: front half of the Wi-Fi -> Ethernet direction and the whole
 * Ethernet -> Wi-Fi direction.
 */
__attribute__((noinline, noreturn)) static void core0_loop(volatile uint32_t *state)
{
	/* TDMA status words 5..14 keep their Linux-visible home in the core 1
	 * block; core 1 itself writes only words 0..4 there.
	 */
	volatile uint32_t *tdma_state =
		(void *)(uintptr_t)(AN7552_NPU_BASE + AN7552_STRIDE);
	uint32_t heartbeat = 0;
	struct tx_band download[2] = { { 0 } };
	struct up_front upload = { 0 };

	PROF_DECLARE(prof, 0);
	for (;;) {
		PROF_TICK(prof, 0);
		park_check(0);
		state[1] = ++heartbeat;
		PROF(prof, 1, native_txfree(0); native_txfree(1));
		/* Replay service, then at most UP_FRONT_BUDGET upload frames,
		 * then download. The front half never reads the station
		 * table, so it also runs during a station quiesce and an RRO
		 * stop never waits on that.
		 */
		PROF(prof, 6, up_pn_service());
		PROF(prof, 5, native_up_front(&upload));
		if (station_quiesced(4))
			continue;
		PROF(prof, 2, native_transmit(0); native_transmit(1));
		/* TDMA ingress, direct transmit and completion reclaim. With
		 * forwarding the TDMA ring is consumed by native_forward_ring()
		 * and re-armed from the buffer pool.
		 */
		for (uint32_t band = 0; band < 2; band++)
			PROF(prof, 3,
			     native_download(tdma_state, &download[band], band));
	}
}

void firmware_main(uint32_t core)
{
	volatile uint32_t *state =
		(void *)(uintptr_t)(AN7552_NPU_BASE + core * AN7552_STRIDE);

	state[1] = 0;
	state[2] = 0;
	state[3] = 0;
	state[4] = 0;
	*(volatile uint32_t *)(uintptr_t)(AN7552_NPU_BASE +
					  AN7552_FW_RUNTIME_ABI + core * 4) =
		AN7552_FW_ABI_VERSION;
	if (!core)
		tx_pool_boot();
	up_init(core);
	barrier();
	state[0] = AN7552_MAGIC;
	if (core)
		core1_loop(state);
	core0_loop(state);
}
