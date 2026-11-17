// SPDX-License-Identifier: GPL-2.0

#include "../perf_regs.h"
#include "../../arch/ia64/include/perf_regs.h"

uint64_t __perf_reg_mask_ia64(bool intr __maybe_unused)
{
	return PERF_REGS_MASK;
}

static const char *const ia64_reg_names[PERF_REG_IA64_MAX] = {
	"%r0", "%r1", "%r2", "%r3", "%r4", "%r5", "%r6", "%r7",
	"%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
	"%r16", "%r17", "%r18", "%r19", "%r20", "%r21", "%r22", "%r23",
	"%r24", "%r25", "%r26", "%r27", "%r28", "%r29", "%r30", "%r31",
	"%b0", "%b1", "%b2", "%b3", "%b4", "%b5", "%b6", "%b7",
	"ip", "psr", "cfm", "pr", "bsp", "bspstore", "rnat", "pfs",
	"unat", "rsc", "fpsr", "ccv",
};

const char *__perf_reg_name_ia64(int id)
{
	if (id < 0 || id >= PERF_REG_IA64_MAX)
		return NULL;
	return ia64_reg_names[id];
}

uint64_t __perf_reg_ip_ia64(void)
{
	return PERF_REG_IA64_IP;
}

uint64_t __perf_reg_sp_ia64(void)
{
	return PERF_REG_IA64_R12;
}

/*
 * Registers that a frame-by-frame unwind needs first: IP and the RSE state
 * (CFM, BSP, BSPSTORE, PFS), predicates, the NaT collections, the
 * stack-pointer and gp, and the return pointer.  Used as the DWARF minimal
 * set in place of IP|SP, which is not enough on ia64.
 */
uint64_t __perf_reg_minimal_ia64(void)
{
	return (1ULL << PERF_REG_IA64_IP) |
	       (1ULL << PERF_REG_IA64_PSR) |
	       (1ULL << PERF_REG_IA64_CFM) |
	       (1ULL << PERF_REG_IA64_PFS) |
	       (1ULL << PERF_REG_IA64_BSP) |
	       (1ULL << PERF_REG_IA64_BSPSTORE) |
	       (1ULL << PERF_REG_IA64_PR) |
	       (1ULL << PERF_REG_IA64_RNAT) |
	       (1ULL << PERF_REG_IA64_UNAT) |
	       (1ULL << PERF_REG_IA64_R1) |
	       (1ULL << PERF_REG_IA64_R12) |
	       (1ULL << PERF_REG_IA64_R13) |
	       (1ULL << PERF_REG_IA64_B0);
}
