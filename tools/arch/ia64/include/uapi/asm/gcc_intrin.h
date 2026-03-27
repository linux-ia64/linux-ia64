/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_IA64_GCC_INTRIN_H
#define _ASM_IA64_GCC_INTRIN_H

#define ia64_mf()	asm volatile ("mf" ::: "memory")

#endif /* _ASM_IA64_GCC_INTRIN_H */
