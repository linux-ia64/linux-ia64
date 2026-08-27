// SPDX-License-Identifier: GPL-2.0
/*
 * Performance events support for the IA-64 performance monitoring unit.
 *
 * This replaces the old perfmon interface (removed in commit ecf5b72d5f66,
 * "ia64: Remove perfmon") with a plain perf_events PMU driver.  The register
 * descriptions of the individual Itanium implementations are kept in separate
 * headers, one per PMU model, the same way perfmon used to organise them.
 *
 * Only the counting monitors (PMC/PMD4 and up) are driven here.  The Itanium
 * PMUs have plenty more to offer -- event address registers, the branch trace
 * buffer, opcode matchers and address range checking -- none of which maps
 * onto a perf counter; those stay unused.
 *
 * Copyright (C) 2026 Tomas Glozar
 *
 * Derived from the perfmon PMU descriptions by
 * Copyright (C) 2002-2003 Hewlett Packard Co
 *		 Stephane Eranian <eranian@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/perf_event.h>
#include <linux/sched/task_stack.h>
#include <linux/smp.h>
#include <linux/sysfs.h>

#include <asm/hw_irq.h>
#include <asm/pal.h>
#include <asm/perf_event.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/switch_to.h>

#include "irq.h"

#define IA64_PMU_FIRST_COUNTER	4
#define IA64_PMU_MAX_COUNTERS	16	/* PMD4..PMD15 on Montecito */

#define IA64_PMU_ANY_FAMILY	0xff

/*
 * An entry of the canned event tables: the set of counters able to count the
 * event in bits 63..48, and the PMC event selection bits in 31..0.  An all
 * zero entry means "this event does not exist on this model".
 *
 * IA64_EVENT() takes the event code in the encoding libpfm uses (event select
 * in bits 7..0, unit mask in bits 19..16) so that the tables can be checked
 * against libpfm's event lists by eye.
 */
#define IA64_EVENT(counters, code)					   \
	(((u64)(counters) << 48) |					   \
	 (((u64)(code) & 0xff) << IA64_PMC_ES_SHIFT) |			   \
	 ((((u64)(code) >> 16) & 0xf) << IA64_PMC_UMASK_SHIFT))

#define IA64_EVENT_COUNTERS(e)	((unsigned long)((e) >> 48))
#define IA64_EVENT_CONFIG(e)	((e) & 0xffffffffUL)

#define C(x)			PERF_COUNT_HW_CACHE_##x

struct ia64_pmu_model {
	const char	*name;
	unsigned int	family;		/* CPUID[3].family, or ANY	     */
	unsigned int	num_counters;	/* PMC/PMD pairs starting at 4	     */
	unsigned int	counter_bits;	/* implemented width of a PMD	     */
	u64		event_mask;	/* PMC bits the user may choose	     */
	u64		pmc_fixed;	/* PMC bits the kernel always sets   */
	const u64	*generic_events;
	const u64	(*cache_events)[PERF_COUNT_HW_CACHE_OP_MAX]
				       [PERF_COUNT_HW_CACHE_RESULT_MAX];
};

#include "perf_event_gen.h"

/*
 * Probed in family order, generic last: it matches anything.
 */
static struct ia64_pmu_model * const ia64_pmu_models[] = {
	&ia64_pmu_gen,
};

static struct ia64_pmu_model *ia64_pmu_model;
static u64 ia64_pmu_counter_mask;	/* value mask of one PMD	     */
static unsigned long ia64_pmu_counters;	/* bitmap of usable PMD indices	     */

struct cpu_hw_events {
	struct perf_event	*events[IA64_PMU_MAX_COUNTERS];
	unsigned long		used_mask;
	unsigned int		n_active;
	bool			enabled;
};

static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

/*
 * Number of active monitors on this CPU that count user level code.  While
 * this is non-zero the incoming task's psr.pp has to be turned on during a
 * context switch, otherwise the monitors would only ever see kernel code (the
 * kernel side is covered by dcr.pp, which is set once at init time).
 */
DEFINE_PER_CPU(unsigned int, ia64_pmu_pp_users);

static inline void ia64_pmu_freeze(void)
{
	ia64_set_pmc(0, IA64_PMC0_FR);
	ia64_srlz_d();
}

static inline void ia64_pmu_unfreeze(void)
{
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}

/*
 * Let the currently running task count at user level (or stop it from doing
 * so).  Kernel entries pick psr.pp up from dcr.pp on their own, only the
 * value the interrupted context resumes with needs fixing up.
 */
static inline void ia64_pmu_set_user_pp(int on)
{
	ia64_psr(task_pt_regs(current))->pp = on;
}

/*
 * Called from __switch_to() via ia64_{save,load}_extra() while at least one
 * monitor on this CPU counts user level code.
 */
void ia64_pmu_switch_task(struct task_struct *task, int sched_in)
{
	ia64_psr(task_pt_regs(task))->pp = sched_in;
}

static u64 ia64_pmu_read_counter(int idx)
{
	return ia64_get_pmd(idx) & ia64_pmu_counter_mask;
}

static void ia64_pmu_write_counter(int idx, u64 val)
{
	ia64_set_pmd(idx, val & ia64_pmu_counter_mask);
	ia64_srlz_d();
}

/*
 * Fold the hardware counter into event->count.  The counters count upwards
 * and wrap at counter_bits, so the difference has to be masked as well.
 */
static void ia64_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, now, delta;

	do {
		prev = local64_read(&hwc->prev_count);
		now  = ia64_pmu_read_counter(hwc->idx);
	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	delta = (now - prev) & ia64_pmu_counter_mask;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);
}

/*
 * Arm the counter so that it overflows after sample_period events.
 */
static int ia64_pmu_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 max_period = ia64_pmu_counter_mask >> 1;
	s64 period = hwc->sample_period;
	s64 left = local64_read(&hwc->period_left);
	int overflow = 0;

	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		overflow = 1;
	}

	if (left > max_period)
		left = max_period;

	local64_set(&hwc->prev_count, -left & ia64_pmu_counter_mask);
	ia64_pmu_write_counter(hwc->idx, -left);

	perf_event_update_userpage(event);

	return overflow;
}

static void ia64_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(hwc->idx < 0))
		return;
	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
		ia64_pmu_event_set_period(event);
	}

	hwc->state = 0;

	if (hwc->config_base & IA64_PMC_PLM_USER) {
		this_cpu_inc(ia64_pmu_pp_users);
		ia64_pmu_set_user_pp(1);
	}

	ia64_set_pmc(hwc->idx, hwc->config | hwc->config_base);
	ia64_srlz_d();
}

static void ia64_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		/* plm == 0 disables the monitor */
		ia64_set_pmc(hwc->idx, ia64_pmu_model->pmc_fixed);
		ia64_srlz_d();

		if ((hwc->config_base & IA64_PMC_PLM_USER) &&
		    this_cpu_dec_return(ia64_pmu_pp_users) == 0)
			ia64_pmu_set_user_pp(0);

		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		ia64_pmu_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int ia64_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	unsigned long avail;
	int idx;

	avail = hwc->event_base & ~cpuc->used_mask;
	if (!avail)
		return -EAGAIN;

	idx = __ffs(avail);

	cpuc->used_mask |= 1UL << idx;
	cpuc->events[idx] = event;
	cpuc->n_active++;
	hwc->idx = idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		ia64_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

static void ia64_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	ia64_pmu_stop(event, PERF_EF_UPDATE);

	cpuc->events[hwc->idx] = NULL;
	cpuc->used_mask &= ~(1UL << hwc->idx);
	cpuc->n_active--;
	hwc->idx = -1;

	perf_event_update_userpage(event);
}

static void ia64_pmu_read(struct perf_event *event)
{
	if (event->hw.idx >= 0)
		ia64_pmu_event_update(event);
}

static void ia64_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (cpuc->enabled)
		return;

	cpuc->enabled = true;
	ia64_pmu_unfreeze();
}

static void ia64_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!cpuc->enabled)
		return;

	cpuc->enabled = false;
	ia64_pmu_freeze();
}

static int ia64_pmu_map_cache_event(u64 config, u64 *event)
{
	unsigned int type, op, result;
	u64 evt;

	type   = (config >>  0) & 0xff;
	op     = (config >>  8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	if (!ia64_pmu_model->cache_events)
		return -EOPNOTSUPP;

	evt = ia64_pmu_model->cache_events[type][op][result];
	if (!evt)
		return -EOPNOTSUPP;

	*event = evt;
	return 0;
}

static int ia64_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned long counters;
	u64 evt = 0;
	u64 plm = 0;
	int ret;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		if (event->attr.config >= PERF_COUNT_HW_MAX)
			return -EINVAL;
		evt = ia64_pmu_model->generic_events[event->attr.config];
		if (!evt)
			return -EOPNOTSUPP;
		break;
	case PERF_TYPE_HW_CACHE:
		ret = ia64_pmu_map_cache_event(event->attr.config, &evt);
		if (ret)
			return ret;
		break;
	case PERF_TYPE_RAW:
		/*
		 * The raw config is the value of the counter's PMC: the event
		 * select sits in bits 15..8, the unit mask in 19..16, and so
		 * on.  Everything the kernel drives itself is masked out.
		 */
		evt = event->attr.config & ia64_pmu_model->event_mask;
		evt |= (u64)ia64_pmu_counters << 48;
		break;
	default:
		return -ENOENT;
	}

	if (event->attr.exclude_idle)
		return -EOPNOTSUPP;
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	if (!event->attr.exclude_kernel)
		plm |= IA64_PMC_PLM_KERN;
	if (!event->attr.exclude_user)
		plm |= IA64_PMC_PLM_USER;
	if (!plm)
		return -EINVAL;

	counters = IA64_EVENT_COUNTERS(evt) & ia64_pmu_counters;
	if (!counters)
		return -EOPNOTSUPP;

	/*
	 * The monitors are privileged (pm=1) and thus gated by psr.pp, which
	 * the kernel owns; a monitored task cannot turn counting off behind
	 * perf's back the way it could with the user monitors psr.up gates.
	 */
	hwc->config = (IA64_EVENT_CONFIG(evt) & ia64_pmu_model->event_mask) |
		      ia64_pmu_model->pmc_fixed;
	hwc->config_base = plm | IA64_PMC_PM | IA64_PMC_OI;
	hwc->event_base = counters;
	hwc->idx = -1;

	/*
	 * Even a counting event needs to overflow now and then, otherwise the
	 * 32 bit counters of the first Itaniums wrap unnoticed within seconds.
	 */
	if (!is_sampling_event(event)) {
		hwc->sample_period = ia64_pmu_counter_mask >> 1;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	return 0;
}

static irqreturn_t ia64_pmu_interrupt(int irq, void *arg)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct pt_regs *regs = get_irq_regs();
	struct perf_sample_data data;
	unsigned long ovfl;
	int idx;

	/*
	 * An overflowing monitor with pmc.oi set freezes the whole PMU and
	 * records itself in pmc0; the handler runs with all monitors stopped.
	 */
	ovfl = ia64_get_pmc(0) & IA64_PMC0_OVFL_MASK & ia64_pmu_counters;

	for_each_set_bit(idx, &ovfl, IA64_PMU_MAX_COUNTERS) {
		struct perf_event *event = cpuc->events[idx];

		if (!event)
			continue;

		ia64_pmu_event_update(event);

		if (!ia64_pmu_event_set_period(event))
			continue;

		perf_sample_data_init(&data, 0, event->hw.last_period);

		if (perf_event_overflow(event, &data, regs))
			ia64_pmu_stop(event, 0);
	}

	/* clears the overflow status bits as a side effect */
	if (cpuc->enabled)
		ia64_pmu_unfreeze();
	else
		ia64_pmu_freeze();

	return IRQ_HANDLED;
}

PMU_FORMAT_ATTR(event,	"config:8-15");
PMU_FORMAT_ATTR(umask,	"config:16-19");
PMU_FORMAT_ATTR(thres,	"config:20-22");

static struct attribute *ia64_pmu_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_thres.attr,
	NULL,
};

static const struct attribute_group ia64_pmu_format_group = {
	.name		= "format",
	.attrs		= ia64_pmu_format_attrs,
};

static const struct attribute_group *ia64_pmu_attr_groups[] = {
	&ia64_pmu_format_group,
	NULL,
};

static struct pmu ia64_pmu = {
	.attr_groups	= ia64_pmu_attr_groups,
	.pmu_enable	= ia64_pmu_enable,
	.pmu_disable	= ia64_pmu_disable,
	.event_init	= ia64_pmu_event_init,
	.add		= ia64_pmu_add,
	.del		= ia64_pmu_del,
	.start		= ia64_pmu_start,
	.stop		= ia64_pmu_stop,
	.read		= ia64_pmu_read,
	.capabilities	= PERF_PMU_CAP_NO_NMI,
};

void perf_event_print_debug(void)
{
	unsigned long flags;
	int cpu, idx;

	if (!ia64_pmu_model)
		return;

	local_irq_save(flags);
	cpu = smp_processor_id();

	pr_info("CPU#%d: pmc0: 0x%016lx dcr.pp: %d psr.pp: %d pp_users: %u\n",
		cpu, ia64_get_pmc(0),
		!!(ia64_getreg(_IA64_REG_CR_DCR) & IA64_DCR_PP),
		!!(ia64_getreg(_IA64_REG_PSR) & IA64_PSR_PP),
		__this_cpu_read(ia64_pmu_pp_users));

	for_each_set_bit(idx, &ia64_pmu_counters, IA64_PMU_MAX_COUNTERS)
		pr_info("CPU#%d: pmc%-2d: 0x%016lx pmd%-2d: 0x%016lx\n",
			cpu, idx, ia64_get_pmc(idx), idx, ia64_get_pmd(idx));

	local_irq_restore(flags);
}

/*
 * Bring this CPU's PMU into the state the driver expects: no measurement
 * running (the firmware may have left monitors programmed), monitors
 * unfrozen, overflows delivered to IA64_PERFMON_VECTOR and dcr.pp set so that
 * privileged monitors keep counting once the kernel is entered.
 *
 * Runs on the boot CPU from ia64_pmu_init() and on every other CPU from
 * smp_callin().  Only the first caller registers the interrupt handler; the
 * secondary CPUs come through here with interrupts off and must not allocate.
 */
void ia64_pmu_init_percpu(void)
{
	static bool irq_registered;
	int idx;

	if (!ia64_pmu_model)
		return;

	ia64_rsm(IA64_PSR_PP);
	ia64_rsm(IA64_PSR_UP);
	ia64_srlz_i();

	ia64_pmu_freeze();

	for_each_set_bit(idx, &ia64_pmu_counters, IA64_PMU_MAX_COUNTERS) {
		ia64_set_pmc(idx, ia64_pmu_model->pmc_fixed);
		ia64_set_pmd(idx, 0);
	}
	ia64_srlz_d();

	if (!irq_registered) {
		register_percpu_irq(IA64_PERFMON_VECTOR, ia64_pmu_interrupt, 0,
				    "perfmon");
		irq_registered = true;
	}

	ia64_setreg(_IA64_REG_CR_PMV, IA64_PERFMON_VECTOR);
	ia64_srlz_d();

	/*
	 * Privileged monitors are gated by psr.pp; dcr.pp makes every
	 * interruption turn it back on so that kernel code is covered without
	 * a hook on every kernel entry.  Nothing counts until a monitor is
	 * given a non-zero plm.
	 */
	ia64_setreg(_IA64_REG_CR_DCR, ia64_getreg(_IA64_REG_CR_DCR) |
				      IA64_DCR_PP);
	ia64_ssm(IA64_PSR_PP);
	ia64_srlz_i();

	ia64_pmu_unfreeze();
	this_cpu_write(cpu_hw_events.enabled, true);
}

/*
 * PAL_PERF_MON_INFO describes the architected part of any implementation, so
 * it can tell us how many counters there are, how wide they are and which
 * event selects count cycles and retired instructions.  Used to give the
 * generic model something to work with on a processor we know nothing about.
 */
static int __init ia64_pmu_probe_generic(void)
{
	pal_perf_mon_info_u_t pm_info;
	u64 pm_buffer[16];
	unsigned int generic, width;

	if (ia64_pal_perf_mon_info(pm_buffer, &pm_info) != 0)
		return -ENODEV;

	generic = pm_info.pal_perf_mon_info_s.generic;
	width   = pm_info.pal_perf_mon_info_s.width;

	if (!generic || !width || width > 64)
		return -ENODEV;

	if (generic > IA64_PMU_MAX_COUNTERS - IA64_PMU_FIRST_COUNTER)
		generic = IA64_PMU_MAX_COUNTERS - IA64_PMU_FIRST_COUNTER;

	ia64_pmu_gen.num_counters = generic;
	ia64_pmu_gen.counter_bits = width;

	ia64_gen_generic_events[PERF_COUNT_HW_CPU_CYCLES] =
		IA64_EVENT(pm_buffer[8] & 0xffff,
			   pm_info.pal_perf_mon_info_s.cycles);
	ia64_gen_generic_events[PERF_COUNT_HW_INSTRUCTIONS] =
		IA64_EVENT(pm_buffer[12] & 0xffff,
			   pm_info.pal_perf_mon_info_s.retired);

	return 0;
}

static int __init ia64_pmu_probe(void)
{
	unsigned int family = local_cpu_data->family;
	struct ia64_pmu_model *model;
	int i;

	for (i = 0; i < ARRAY_SIZE(ia64_pmu_models); i++) {
		model = ia64_pmu_models[i];

		if (model->family != IA64_PMU_ANY_FAMILY &&
		    model->family != family)
			continue;

		if (model == &ia64_pmu_gen && ia64_pmu_probe_generic())
			continue;

		ia64_pmu_model = model;
		return 0;
	}

	return -ENODEV;
}

static int __init ia64_pmu_init(void)
{
	int ret;

	if (ia64_pmu_probe()) {
		pr_info("perf: no PMU support for processor family %u\n",
			local_cpu_data->family);
		return -ENODEV;
	}

	ia64_pmu_counter_mask = (1UL << ia64_pmu_model->counter_bits) - 1;
	ia64_pmu_counters = ((1UL << ia64_pmu_model->num_counters) - 1)
			    << IA64_PMU_FIRST_COUNTER;

	pr_info("perf: %s PMU detected, %u counters (%u bits), IRQ %u\n",
		ia64_pmu_model->name, ia64_pmu_model->num_counters,
		ia64_pmu_model->counter_bits, IA64_PERFMON_VECTOR);

	ret = perf_pmu_register(&ia64_pmu, "cpu", PERF_TYPE_RAW);
	if (ret) {
		ia64_pmu_model = NULL;
		return ret;
	}

	/*
	 * This is an early initcall, so it runs on the boot CPU before the
	 * others are brought up; they set themselves up from smp_callin().
	 */
	ia64_pmu_init_percpu();

	return 0;
}
early_initcall(ia64_pmu_init);
