/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Architected (model independent) IA-64 PMU description.
 *
 * Only PMC/PMD4..7 and the event select field are architected, so nothing but
 * the two events PAL_PERF_MON_INFO reports (CPU cycles and retired
 * instructions) can be named here.  The event codes, the number of counters
 * and the counter width are filled in from PAL at probe time, see
 * ia64_pmu_probe_generic().
 */

static u64 ia64_gen_generic_events[PERF_COUNT_HW_MAX];

static struct ia64_pmu_model ia64_pmu_gen = {
	.name		= "Generic",
	.family		= IA64_PMU_ANY_FAMILY,
	.num_counters	= 4,
	.counter_bits	= 32,
	.event_mask	= 0xffUL << IA64_PMC_ES_SHIFT,
	.pmc_fixed	= IA64_PMC_ISM_IA64,
	.generic_events	= ia64_gen_generic_events,
};
