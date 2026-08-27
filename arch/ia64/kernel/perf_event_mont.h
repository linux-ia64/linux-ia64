/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dual-core Itanium 2 ("Montecito", "Montvale", CPUID family 0x20) PMU
 * description.
 *
 * Twelve 47 bit counters in PMD4..PMD15.  PMC.ism has to be 2 (IA-64 only),
 * bit 26 selects whether a monitor counts for this thread only or for both
 * threads of the core, and bits 27..30 are the MESI qualifier that the cache
 * events are filtered by -- an L2D/L3 event with an empty MESI mask counts
 * nothing, so the canned events below ask for all four states.
 *
 * Event codes are in libpfm encoding: event select in bits 7..0, unit mask in
 * bits 19..16.
 */

static const u64 ia64_mont_generic_events[PERF_COUNT_HW_MAX] = {
	/* CPU_OP_CYCLES_ALL */
	[PERF_COUNT_HW_CPU_CYCLES]		= IA64_EVENT(0xfff0, 0x12),
	[PERF_COUNT_HW_INSTRUCTIONS]		= IA64_EVENT(0xfff0, 0x8),
	[PERF_COUNT_HW_CACHE_REFERENCES]	= IA64_EVENT(0xfff0, 0xdb) |
						  IA64_PMC_MESI_ALL,
	[PERF_COUNT_HW_CACHE_MISSES]		= IA64_EVENT(0xfff0, 0xdc) |
						  IA64_PMC_MESI_ALL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= IA64_EVENT(0xfff0, 0x5b),
	[PERF_COUNT_HW_BRANCH_MISSES]		= IA64_EVENT(0xfff0, 0x2005b),
};

static const u64 ia64_mont_cache_events
		[PERF_COUNT_HW_CACHE_MAX]
		[PERF_COUNT_HW_CACHE_OP_MAX]
		[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
 [C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0xc2),	/* L1D_READS_SET0 */
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0xc7),	/* L1D_READ_MISSES_ALL */
	},
 },
 [C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0x40),	/* L1I_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0x41),	/* L1I_FILLS */
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0x44),	/* L1I_PREFETCHES */
	},
 },
 [C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0xdb) |	/* L3_REFERENCES */
				     IA64_PMC_MESI_ALL,
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0xdc) |	/* L3_MISSES */
				     IA64_PMC_MESI_ALL,
	},
 },
 [C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0xc3),	/* DATA_REFERENCES_SET0 */
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0xc1),	/* L2DTLB_MISSES */
	},
 },
 [C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0x40),	/* L1I_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0x30047), /* ITLB_MISSES_FETCH_ALL */
	},
 },
 [C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xfff0, 0x5b),
		[C(RESULT_MISS)]   = IA64_EVENT(0xfff0, 0x2005b),
	},
 },
};

static struct ia64_pmu_model ia64_pmu_mont = {
	.name		= "Montecito",
	.family		= 0x20,
	.num_counters	= 12,
	.counter_bits	= 47,
	/* es[15:8], umask[19:16], thres[22:20], all[26], mesi[30:27] */
	.event_mask	= 0x787fff00UL | IA64_PMC_ALL,
	.pmc_fixed	= IA64_PMC_ISM_IA64,
	.generic_events	= ia64_mont_generic_events,
	.cache_events	= ia64_mont_cache_events,
};
