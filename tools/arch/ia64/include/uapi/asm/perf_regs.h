/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_IA64_PERF_REGS_H
#define _UAPI_ASM_IA64_PERF_REGS_H

#include <linux/types.h>

/*
 * Register numbers for perf's sample_regs_user / sample_regs_intr.
 *
 * Values come from pt_regs and the RSE state, not from the full register
 * file: r4-r7 and b1-b5 are preserved registers that pt_regs does not save,
 * so they read as 0.  Registers that do not exist for the sample read as 0.
 *
 * BSP is the end of the frame's backing store, i.e. the value ptrace reports
 * as the user rbs end (the "after cover" value, as gdb uses it).  A consumer
 * that wants the start of the current frame computes it with
 * rse_skip_regs(BSP, -(CFM & 0x7f)).
 *
 * CFM is the interrupted frame's cfm, with the valid bit cleared.  PFS is
 * ar.pfs (the previous frame state), a different register from CFM.
 */
enum perf_event_ia64_regs {
	PERF_REG_IA64_R0,
	PERF_REG_IA64_R1,
	PERF_REG_IA64_R2,
	PERF_REG_IA64_R3,
	PERF_REG_IA64_R4,
	PERF_REG_IA64_R5,
	PERF_REG_IA64_R6,
	PERF_REG_IA64_R7,
	PERF_REG_IA64_R8,
	PERF_REG_IA64_R9,
	PERF_REG_IA64_R10,
	PERF_REG_IA64_R11,
	PERF_REG_IA64_R12,
	PERF_REG_IA64_R13,
	PERF_REG_IA64_R14,
	PERF_REG_IA64_R15,
	PERF_REG_IA64_R16,
	PERF_REG_IA64_R17,
	PERF_REG_IA64_R18,
	PERF_REG_IA64_R19,
	PERF_REG_IA64_R20,
	PERF_REG_IA64_R21,
	PERF_REG_IA64_R22,
	PERF_REG_IA64_R23,
	PERF_REG_IA64_R24,
	PERF_REG_IA64_R25,
	PERF_REG_IA64_R26,
	PERF_REG_IA64_R27,
	PERF_REG_IA64_R28,
	PERF_REG_IA64_R29,
	PERF_REG_IA64_R30,
	PERF_REG_IA64_R31,
	PERF_REG_IA64_B0,
	PERF_REG_IA64_B1,
	PERF_REG_IA64_B2,
	PERF_REG_IA64_B3,
	PERF_REG_IA64_B4,
	PERF_REG_IA64_B5,
	PERF_REG_IA64_B6,
	PERF_REG_IA64_B7,
	PERF_REG_IA64_IP,	/* bundle address | psr.ri */
	PERF_REG_IA64_PSR,
	PERF_REG_IA64_CFM,
	PERF_REG_IA64_PR,
	PERF_REG_IA64_BSP,	/* end of the frame's backing store */
	PERF_REG_IA64_BSPSTORE,
	PERF_REG_IA64_RNAT,
	PERF_REG_IA64_PFS,
	PERF_REG_IA64_UNAT,
	PERF_REG_IA64_RSC,
	PERF_REG_IA64_FPSR,
	PERF_REG_IA64_CCV,
	PERF_REG_IA64_MAX,
};

/*
 * User stack dump for PERF_SAMPLE_STACK_USER on ia64 (see perf_regs.c for the
 * algorithm).  The dump is self-describing:
 *
 *	struct perf_ia64_ustack_hdr	(hdr.rbs_len bytes of RBS image)
 *	rbs image			covers [rbs_addr, rbs_addr + rbs_len)
 *	memory stack			covers [mem_addr, mem_addr + mem_len)
 *
 * The RBS image is the user register backing store as it was at the sample,
 * with the NaT collection slots zeroed.  The memory stack starts at r12.
 * Consumers check magic and the lengths before indexing.
 */
#define PERF_IA64_USTACK_MAGIC	0x494136345553544bULL	/* "IA64USTK", big-endian */

struct perf_ia64_ustack_hdr {
	__u64	magic;		/* PERF_IA64_USTACK_MAGIC */
	__u64	rbs_addr;	/* user VA of the first RBS byte in the dump */
	__u64	rbs_len;	/* bytes of RBS image, a multiple of 8 */
	__u64	mem_addr;	/* user VA of the first memory-stack byte (r12) */
	__u64	mem_len;	/* bytes of memory stack in the dump */
};

#endif /* _UAPI_ASM_IA64_PERF_REGS_H */
