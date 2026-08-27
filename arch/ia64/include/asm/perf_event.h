/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IA-64 performance monitoring unit definitions.
 *
 * The layout below is the architected one (Itanium SDM, vol 2, "Performance
 * Monitoring").  Bits 0..7 of a counter configuration register (PMC4 and up)
 * are architected and driven by the kernel; the event selection bits above
 * them are model specific and come from the raw perf_event config.
 */
#ifndef _ASM_IA64_PERF_EVENT_H
#define _ASM_IA64_PERF_EVENT_H

/* PMC4..PMCn: counter configuration registers */
#define IA64_PMC_PLM_SHIFT	0		/* privilege level mask	     */
#define IA64_PMC_PLM_MASK	(0xfUL << IA64_PMC_PLM_SHIFT)
#define IA64_PMC_PLM_KERN	(1UL << 0)	/* count at privilege level 0 */
#define IA64_PMC_PLM_USER	(1UL << 3)	/* count at privilege level 3 */
#define IA64_PMC_EV		(1UL << 4)	/* external visibility	     */
#define IA64_PMC_OI		(1UL << 5)	/* overflow interrupt	     */
#define IA64_PMC_PM		(1UL << 6)	/* privileged monitor (psr.pp) */
#define IA64_PMC_ES_SHIFT	8		/* event select		     */
#define IA64_PMC_UMASK_SHIFT	16		/* unit mask		     */
#define IA64_PMC_THRES_SHIFT	20		/* threshold		     */
#define IA64_PMC_ISM_SHIFT	24		/* instruction set mask	     */
#define IA64_PMC_ISM_IA64	(2UL << IA64_PMC_ISM_SHIFT)
#define IA64_PMC_ALL		(1UL << 26)	/* Montecito: both threads   */
#define IA64_PMC_MESI_SHIFT	27		/* Montecito: cache line state */
#define IA64_PMC_MESI_ALL	(0xfUL << IA64_PMC_MESI_SHIFT)

/* PMC0: freeze bit plus one overflow status bit per PMD */
#define IA64_PMC0_FR		(1UL << 0)
#define IA64_PMC0_OVFL_MASK	(~1UL)

#ifdef CONFIG_PERF_EVENTS
extern void ia64_pmu_init_percpu(void);
#else
static inline void ia64_pmu_init_percpu(void) { }
#endif

#endif /* _ASM_IA64_PERF_EVENT_H */
