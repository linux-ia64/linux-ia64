// SPDX-License-Identifier: GPL-2.0
/*
 * perf register sampling for ia64: maps PERF_REG_IA64_* to pt_regs and the
 * RSE state.  See uapi/asm/perf_regs.h for the register numbering.
 */
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/bug.h>
#include <linux/uaccess.h>
#include <linux/minmax.h>
#include <linux/string.h>

#include <asm/intrinsics.h>
#include <asm/kregs.h>
#include <asm/ptrace.h>
#include <asm/rse.h>

u64 perf_reg_abi(struct task_struct *tsk)
{
	return PERF_SAMPLE_REGS_ABI_64;
}

int perf_reg_validate(u64 mask)
{
	if (!mask)
		return -EINVAL;
	if (mask & ~((1ULL << PERF_REG_IA64_MAX) - 1))
		return -EINVAL;
	return 0;
}

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	unsigned long cfm, bsp;

	if (WARN_ON_ONCE((u32)idx >= PERF_REG_IA64_MAX))
		return 0;

	switch (idx) {
	/* General registers.  r0 is hardwired to 0, r4-r7 are not in pt_regs. */
	case PERF_REG_IA64_R0:
	case PERF_REG_IA64_R4 ... PERF_REG_IA64_R7:
		return 0;
	case PERF_REG_IA64_R1:	return regs->r1;
	case PERF_REG_IA64_R2:	return regs->r2;
	case PERF_REG_IA64_R3:	return regs->r3;
	case PERF_REG_IA64_R8:	return regs->r8;
	case PERF_REG_IA64_R9:	return regs->r9;
	case PERF_REG_IA64_R10:	return regs->r10;
	case PERF_REG_IA64_R11:	return regs->r11;
	case PERF_REG_IA64_R12:	return regs->r12;
	case PERF_REG_IA64_R13:	return regs->r13;
	case PERF_REG_IA64_R14:	return regs->r14;
	case PERF_REG_IA64_R15:	return regs->r15;
	case PERF_REG_IA64_R16:	return regs->r16;
	case PERF_REG_IA64_R17:	return regs->r17;
	case PERF_REG_IA64_R18:	return regs->r18;
	case PERF_REG_IA64_R19:	return regs->r19;
	case PERF_REG_IA64_R20:	return regs->r20;
	case PERF_REG_IA64_R21:	return regs->r21;
	case PERF_REG_IA64_R22:	return regs->r22;
	case PERF_REG_IA64_R23:	return regs->r23;
	case PERF_REG_IA64_R24:	return regs->r24;
	case PERF_REG_IA64_R25:	return regs->r25;
	case PERF_REG_IA64_R26:	return regs->r26;
	case PERF_REG_IA64_R27:	return regs->r27;
	case PERF_REG_IA64_R28:	return regs->r28;
	case PERF_REG_IA64_R29:	return regs->r29;
	case PERF_REG_IA64_R30:	return regs->r30;
	case PERF_REG_IA64_R31:	return regs->r31;

	/* Branch registers.  b1-b5 are preserved and not in pt_regs. */
	case PERF_REG_IA64_B0:	return regs->b0;
	case PERF_REG_IA64_B1 ... PERF_REG_IA64_B5:
		return 0;
	case PERF_REG_IA64_B6:	return regs->b6;
	case PERF_REG_IA64_B7:	return regs->b7;

	case PERF_REG_IA64_IP:	return instruction_pointer(regs);
	case PERF_REG_IA64_PSR:	return regs->cr_ipsr;
	case PERF_REG_IA64_PR:	return regs->pr;

	/*
	 * CFM and BSP describe the interrupted frame.  For a user-mode sample
	 * they come from the RSE state, through ia64_get_user_rbs_end().  For a
	 * kernel-mode sample that helper does not apply, so use the raw
	 * registers saved on entry.
	 */
	case PERF_REG_IA64_CFM:
	case PERF_REG_IA64_BSP:
		if (user_mode(regs)) {
			bsp = ia64_get_user_rbs_end(current, regs, &cfm);
		} else {
			bsp = regs->ar_bspstore;
			cfm = regs->cr_ifs;
		}
		return idx == PERF_REG_IA64_CFM ? cfm : bsp;

	case PERF_REG_IA64_BSPSTORE:	return regs->ar_bspstore;
	case PERF_REG_IA64_RNAT:	return regs->ar_rnat;
	case PERF_REG_IA64_PFS:		return regs->ar_pfs;
	case PERF_REG_IA64_UNAT:	return regs->ar_unat;
	case PERF_REG_IA64_RSC:		return regs->ar_rsc;
	case PERF_REG_IA64_FPSR:	return regs->ar_fpsr;
	case PERF_REG_IA64_CCV:		return regs->ar_ccv;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

/*
 * The user regs are always the last user-entry pt_regs, not the regs of the
 * sample.  For a kernel-mode sample the user state is still there, and its
 * ar_bspstore anchors the kernel RBS mapping (see 2c).
 */
void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}

/*
 * User stack dump: the memory stack plus the register backing store (RBS).
 *
 * Invariant the RBS mapping depends on: on user->kernel entry, SAVE_MIN
 * (minstate.h) saves the user ar.bspstore into pt->ar_bspstore and switches
 * ar.bspstore to krbs = task + IA64_RBS_OFFSET.  So the user RBS word at
 * addr >= pt->ar_bspstore lives at krbs + num_regs(pt->ar_bspstore, addr).
 * The anchor is pt->ar_bspstore.  thread.rbs_bot only clamps the dump range,
 * and it is never used as the mapping anchor.
 *
 * The dump is written in this order (see struct perf_ia64_ustack_hdr):
 *   header, RBS image [rbs_addr, urbs_end), memory stack [r12, r12 + mem_len).
 * urbs_end is ia64_get_user_rbs_end(): the user RBS top at the sample, which
 * includes the dirty partition still on the kernel RBS.
 */

#define IA64_USTK_CHUNK_WORDS	32	/* 256-byte staging buffer */

/*
 * Copy len bytes (a multiple of 8) of user memory at addr into the ring.  A
 * faulting chunk is written as zeros, so the output length stays what the
 * header promised.  Returns the bytes written.
 */
static unsigned long ustk_copy_user(struct perf_output_handle *handle,
				    unsigned long addr, unsigned long len)
{
	u64 buf[IA64_USTK_CHUNK_WORDS];
	unsigned long done = 0;

	while (done < len) {
		unsigned long n = min_t(unsigned long, len - done, sizeof(buf));

		if (copy_from_user_nofault(buf, (void __user *)(addr + done), n))
			memset(buf, 0, n);
		if (perf_output_copy(handle, buf, n))
			break;
		done += n;
	}
	return done;
}

/*
 * Copy the RBS words [from, to) that live on the kernel RBS (addr >=
 * bspstore).  NaT collection slots are written as 0: NaT bits don't matter for
 * unwinding the IP.  Returns the bytes written.
 */
static unsigned long ustk_copy_krbs(struct perf_output_handle *handle,
				    unsigned long *bspstore, unsigned long *krbs,
				    unsigned long from, unsigned long to)
{
	u64 buf[IA64_USTK_CHUNK_WORDS];
	unsigned long done = 0;
	unsigned long addr = from;

	while (addr < to) {
		unsigned int nw = 0;

		while (nw < IA64_USTK_CHUNK_WORDS && addr < to) {
			unsigned long *a = (unsigned long *)addr;

			if (ia64_rse_is_rnat_slot(a))
				buf[nw] = 0;
			else
				buf[nw] = *ia64_rse_skip_regs(krbs,
					ia64_rse_num_regs(bspstore, a));
			nw++;
			addr += 8;
		}
		if (perf_output_copy(handle, buf, nw * 8))
			break;
		done += nw * 8;
	}
	return done;
}

u64 ia64_perf_output_ustack(struct perf_output_handle *handle,
			    struct pt_regs *regs, u64 dump_size)
{
	struct perf_ia64_ustack_hdr hdr;
	unsigned long psr = ia64_getreg(_IA64_REG_PSR);
	unsigned long urbs_end, bspstore_addr, rbs_bot, rbs_budget, rbs_addr;
	unsigned long *bspstore, *krbs;
	unsigned long mem_len, written, n;

	/*
	 * 0. The flushrs spill and the user copies can fault.  That is only
	 * recoverable with interruption collection on, so with psr.ic clear
	 * emit nothing.
	 */
	if (!(psr & IA64_PSR_IC))
		return 0;

	/*
	 * 1. A fsys handler is running on the user stack (thread.on_ustack).
	 * Its pt_regs are not usable for this dump.
	 */
	if (current->thread.on_ustack)
		return 0;

	if (dump_size < sizeof(hdr))
		return 0;

	/*
	 * 2. Spill the user's dirty partition (and the kernel frames above it)
	 * to the kernel RBS.  SAVE_MIN sets RSE to eager mode, so user
	 * registers may still be in physical stacked registers at this point.
	 */
	ia64_flushrs();

	/* 3. Anchors. */
	urbs_end = ia64_get_user_rbs_end(current, regs, NULL);
	bspstore_addr = regs->ar_bspstore;
	bspstore = (unsigned long *)bspstore_addr;
	krbs = (unsigned long *)current + IA64_RBS_OFFSET / 8;
	rbs_bot = current->thread.rbs_bot;

	if (bspstore_addr < rbs_bot || bspstore_addr > urbs_end) {
		WARN_ON_ONCE(1);
		return 0;
	}

	/*
	 * 4. Budget.  RBS frames are small (sof is about 5-20 registers), so
	 * half the dump covers many frames.  The memory stack gets the rest,
	 * including any unused RBS budget.  rbs_len is clamped to the user RBS
	 * range, so rbs_addr >= rbs_bot.
	 */
	rbs_budget = min_t(unsigned long, urbs_end - rbs_bot,
			   (dump_size - sizeof(hdr)) / 2);
	rbs_budget &= ~7UL;
	rbs_addr = urbs_end - rbs_budget;

	mem_len = dump_size - sizeof(hdr) - rbs_budget;
	mem_len &= ~7UL;

	hdr.magic = PERF_IA64_USTACK_MAGIC;
	hdr.rbs_addr = rbs_addr;
	hdr.rbs_len = rbs_budget;
	hdr.mem_addr = regs->r12;
	hdr.mem_len = mem_len;

	if (perf_output_copy(handle, &hdr, sizeof(hdr)))
		return 0;
	written = sizeof(hdr);

	/* 5. RBS image.  User memory first, then the kernel RBS. */
	if (rbs_addr < bspstore_addr) {
		n = ustk_copy_user(handle, rbs_addr, bspstore_addr - rbs_addr);
		written += n;
		if (n != bspstore_addr - rbs_addr)
			return written;
	}

	{
		unsigned long kfrom = max_t(unsigned long, rbs_addr, bspstore_addr);

		n = ustk_copy_krbs(handle, bspstore, krbs, kfrom, urbs_end);
		written += n;
		if (n != urbs_end - kfrom)
			return written;
	}

	/* 6. Memory stack from r12. */
	n = ustk_copy_user(handle, regs->r12, mem_len);
	written += n;

	/* 7. Bytes actually written; the core skips the rest of the static size. */
	return written;
}
