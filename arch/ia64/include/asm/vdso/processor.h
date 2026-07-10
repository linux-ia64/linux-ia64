/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#ifndef __ASSEMBLY__

#include <asm/intrinsics.h>

#define cpu_relax()	ia64_hint(ia64_hint_pause)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_VDSO_PROCESSOR_H */
