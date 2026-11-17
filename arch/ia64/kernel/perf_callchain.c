// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel callchains for perf on ia64, built on the kernel unwinder.
 */
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/bug.h>

#include <asm/intrinsics.h>
#include <asm/kregs.h>
#include <asm/ptrace.h>
#include <asm/unwind.h>

/* Fake regs (tracepoints, sw events) get a bounded search for their frame. */
#define IA64_CC_FAKE_MAX_SKIP	32

struct ia64_cc {
	struct perf_callchain_entry_ctx *entry;
	struct pt_regs *regs;
	bool fake;
};

/*
 * regs is a per-CPU fake buffer or the PMU interrupt's pt_regs on the
 * current kernel stack.  Only the latter lies in [current, current + STK).
 */
static bool ia64_cc_regs_on_kstack(struct pt_regs *regs)
{
	unsigned long r = (unsigned long)regs;
	unsigned long base = (unsigned long)current;

	return r >= base && r < base + IA64_STK_OFFSET;
}

/*
 * Walk started from the callback's own frame.  Frames up to and including
 * the sampled one are skipped, since they are perf/irq internals.  The sampled
 * ip is already stored by the caller, so the frame matching it is not stored
 * again.  Each later frame is stored as an ip.  Any failure ends the walk.
 */
static void ia64_cc_walk(struct unw_frame_info *info, void *arg)
{
	struct ia64_cc *cc = arg;
	struct pt_regs *regs = cc->regs;
	unsigned long want = regs->cr_iip & ~0xfUL;
	unsigned long ip, sp;
	int skipped = 0;

	if (!cc->fake) {
		/*
		 * Real regs: step until the irq frame whose pt_regs is regs.
		 * Stepping over it lands in the interrupted frame.
		 */
		for (;;) {
			if (unw_is_intr_frame(info)) {
				unw_get_sp(info, &sp);
				if (sp + 16 == (unsigned long)regs)
					break;
			}
			if (unw_unwind(info) < 0)
				return;
		}
		if (unw_unwind(info) < 0)
			return;
		unw_get_ip(info, &ip);
		if (ip != want) {
			WARN_ON_ONCE(1);
			return;
		}
	} else {
		/* Fake regs: search for the frame whose ip is the sampled one. */
		for (;;) {
			unw_get_ip(info, &ip);
			if (ip == want)
				break;
			if (++skipped > IA64_CC_FAKE_MAX_SKIP)
				return;
			if (unw_unwind(info) < 0)
				return;
		}
	}

	/* Positioned on the sampled frame: store everything above it. */
	while (unw_unwind(info) >= 0) {
		unw_get_ip(info, &ip);
		if (!ip || perf_callchain_store(cc->entry, ip))
			break;
	}
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	struct ia64_cc cc = {
		.entry	= entry,
		.regs	= regs,
		.fake	= !ia64_cc_regs_on_kstack(regs),
	};

	/* The sample ip is always stored, even if the walk is skipped. */
	if (perf_callchain_store(entry, instruction_pointer(regs)))
		return;

	/*
	 * Skip the walk when psr.ic is clear (unwind's flushrs and switch_stack
	 * save need interruption collection), or when this CPU already holds
	 * unw.lock (see unw_lock_held_this_cpu()).
	 */
	if (!(ia64_getreg(_IA64_REG_PSR) & IA64_PSR_IC))
		return;
	if (unw_lock_held_this_cpu())
		return;

	unw_init_running(ia64_cc_walk, &cc);
}
