/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Itanium ("Merced", CPUID family 0x7) PMU description.
 *
 * Four 32 bit counters in PMD4..PMD7.  The event select field is 7 bits wide
 * (bit 15 of a PMC is reserved), the unit mask sits in bits 16..19 and the
 * threshold in bits 20..22.
 *
 * Event codes are taken from the Itanium processor reference manual, in the
 * encoding libpfm uses: event select in bits 7..0, unit mask in bits 19..16.
 */

static const u64 ia64_ita_generic_events[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= IA64_EVENT(0xf0, 0x12),
	/* IA64_INST_RETIRED is only wired up to PMD4 and PMD5 */
	[PERF_COUNT_HW_INSTRUCTIONS]		= IA64_EVENT(0x30, 0x8),
	[PERF_COUNT_HW_CACHE_REFERENCES]	= IA64_EVENT(0xf0, 0x7b),
	[PERF_COUNT_HW_CACHE_MISSES]		= IA64_EVENT(0xf0, 0x7c),
	/* BRANCH_MULTIWAY, all paths, all predictions / wrong path */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= IA64_EVENT(0xf0, 0xe),
	[PERF_COUNT_HW_BRANCH_MISSES]		= IA64_EVENT(0xf0, 0x2000e),
};

static const u64 ia64_ita_cache_events
		[PERF_COUNT_HW_CACHE_MAX]
		[PERF_COUNT_HW_CACHE_OP_MAX]
		[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
 [C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x64),	/* L1D_READS_RETIRED */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x66),	/* L1D_READ_MISSES_RETIRED */
	},
 },
 [C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x20),	/* L1I_DEMAND_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x21),	/* L1I_FILLS */
	},
 },
 [C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x7b),	/* L3_REFERENCES */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x7c),	/* L3_MISSES */
	},
 },
 [C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x63),	/* DATA_REFERENCES_RETIRED */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x61),	/* DTLB_MISSES */
	},
 },
 [C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0x20),	/* L1I_DEMAND_READS */
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x27),	/* ITLB_MISSES_FETCH */
	},
 },
 [C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = IA64_EVENT(0xf0, 0xe),
		[C(RESULT_MISS)]   = IA64_EVENT(0xf0, 0x2000e),
	},
 },
};

static struct ia64_pmu_model ia64_pmu_ita = {
	.name		= "Itanium",
	.family		= 0x7,
	.num_counters	= 4,
	.counter_bits	= 32,
	/* es[14:8], umask[19:16], thres[22:20] */
	.event_mask	= 0x0077ff00UL,
	.pmc_fixed	= IA64_PMC_ISM_IA64,
	.generic_events	= ia64_ita_generic_events,
	.cache_events	= ia64_ita_cache_events,
};
