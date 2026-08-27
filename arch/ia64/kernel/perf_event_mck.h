/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Itanium 2 ("McKinley", "Madison", CPUID family 0x1f) PMU description.
 *
 * Four 47 bit counters in PMD4..PMD7.  Compared to Merced the event select
 * field grew to 8 bits and PMC4 gained a power enable bit (23) which the
 * kernel sets unconditionally.
 *
 * Event codes are in libpfm encoding: event select in bits 7..0, unit mask in
 * bits 19..16.
 */

static const u64 ia64_mck_generic_events[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= IA64_EVENT(0xf0, 0x12),
	[PERF_COUNT_HW_INSTRUCTIONS]		= IA64_EVENT(0xf0, 0x8),
	[PERF_COUNT_HW_CACHE_REFERENCES]	= IA64_EVENT(0xf0, 0xdb),
	[PERF_COUNT_HW_CACHE_MISSES]		= IA64_EVENT(0xf0, 0xdc),
	/* BR_MISPRED_DETAIL, all branch types: all predictions / wrong path */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= IA64_EVENT(0xf0, 0x5b),
	[PERF_COUNT_HW_BRANCH_MISSES]		= IA64_EVENT(0xf0, 0x2005b),
};

static const u64 ia64_mck_cache_events
		[PERF_COUNT_HW_CACHE_MAX]
		[PERF_COUNT_HW_CACHE_OP_MAX]
		[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
 [C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0xc2),	/* L1D_READS_SET0 */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0xc7),	/* L1D_READ_MISSES_ALL */
	},
 },
 [C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x40),	/* L1I_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x41),	/* L1I_FILLS */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x44),	/* L1I_PREFETCHES */
	},
 },
 [C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0xdb),	/* L3_REFERENCES */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0xdc),	/* L3_MISSES */
	},
 },
 [C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0xc3),	/* DATA_REFERENCES_SET0 */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0xc1),	/* L2DTLB_MISSES */
	},
 },
 [C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x40),	/* L1I_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x30047),	/* ITLB_MISSES_FETCH_ALL */
	},
 },
 [C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x5b),
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x2005b),
	},
 },
};

static struct ia64_pmu_model ia64_pmu_mck = {
	.name		= "Itanium 2",
	.family		= 0x1f,
	.num_counters	= 4,
	.counter_bits	= 47,
	/* es[15:8], umask[19:16], thres[22:20] */
	.event_mask	= 0x007fff00UL,
	/* PMC4.enable (bit 23) has to be set for any counter to run */
	.pmc_fixed	= IA64_PMC_ISM_IA64 | (1UL << 23),
	.generic_events	= ia64_mck_generic_events,
	.cache_events	= ia64_mck_cache_events,
};
