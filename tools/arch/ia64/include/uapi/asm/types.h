/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _TOOLS_UAPI_ASM_IA64_TYPES_H
#define _TOOLS_UAPI_ASM_IA64_TYPES_H

/*
 * The kernel's <asm/types.h> gives LP64 ia64 the l64 integer types
 * (__u64 == unsigned long).  The tools assume the ll64 ones on every
 * architecture (see PRI_lu64 in perf's util/event.h), and -Werror=format
 * rejects "%llu" with an __u64 argument otherwise.  powerpc64 is handled
 * with __SANE_USERSPACE_TYPES__; ia64's header has no such switch, so
 * provide the ll64 types here, ahead of the system header in the include
 * path.
 */
#include <asm-generic/int-ll64.h>

#ifdef __ASSEMBLY__
# define __IA64_UL(x)		(x)
# define __IA64_UL_CONST(x)	x
#else
# define __IA64_UL(x)		((unsigned long)(x))
# define __IA64_UL_CONST(x)	x##UL
#endif

#endif /* _TOOLS_UAPI_ASM_IA64_TYPES_H */
