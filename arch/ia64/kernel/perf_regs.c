// SPDX-License-Identifier: GPL-2.0
/*
 * perf register sampling for ia64: maps PERF_REG_IA64_* to pt_regs and the
 * RSE state.  See uapi/asm/perf_regs.h for the register numbering.
 */
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/bug.h>

#include <asm/ptrace.h>

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
