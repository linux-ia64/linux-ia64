/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Returns 0 if exception before NUL or reaching the supplied limit (N),
 * a value greater than N if the string is longer than the limit, else
 * strlen.
 *
 * Inputs:
 *	in0:	address of buffer
 *	in1:	string length limit N
 * Outputs:
 *	r8:	0 in case of fault, strlen(buffer)+1 otherwise
 *
 * Copyright (C) 1999, 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2026 EPIC Linux project, Tomas Glozar <tglozar@proton.me>
 */

#include <linux/export.h>
#include <asm/asmmacro.h>

#define ptr	in0
#define n	in1
#define valx	r15
#define valy	r16
#define cnt1	r17
#define cnt2	r18
#define cend	r19
#define hint	r20

GLOBAL_ENTRY(__strnlen_user)
	.prologue
	alloc r2=ar.pfs,2,0,0,0
	.body
	cmp.eq p6,p0=r0,r0
	adds cend=1,n
	EXCLR(.Lexit, ld1 valx=[ptr],1)
	mov cnt1=0
	mov cnt2=1
	;;
.balign 32
.Loop1:
	EXCLR(.Lexit, ld1.s valy=[ptr],1)
	adds cnt1=2,cnt1
	cmp.ne.and p6,p0=valx,r0
	cmp.ne.and p6,p0=cnt2,cend
(p6)	br.sptk .Loop2
	;;
	chk.s valx,.Lfaultx
	adds ret0=-1,cnt1
	br.ret.sptk.many rp
.balign 32
.Loop2:
	EXCLR(.Lexit, ld1.s valx=[ptr],1)
	adds cnt2=2,cnt2
	cmp.ne.and p6,p0=valy,r0
	cmp.ne.and p6,p0=cnt1,cend
(p6)	br.sptk .Loop1
	;;
	chk.s valy,.Lfaulty
	adds ret0=-1,cnt2
	br.ret.sptk.many rp
.Lfaultx:
	adds cnt1=-2,cnt1
	adds ptr=-2,ptr
	cmp.eq p6,p0=r0,r0
	;;
	EXCLR(.Lexit, ld1 valx=[ptr],1)
	br.sptk .Loop1
.Lfaulty:
	adds cnt2=-2,cnt2
	adds ptr=-2,ptr
	cmp.eq p6,p0=r0,r0
	;;
	EXCLR(.Lexit, ld1 valy=[ptr],1)
	br.sptk .Loop2
.Lexit:
	mov ret0=0
	br.ret.sptk.many rp
END(__strnlen_user)
EXPORT_SYMBOL(__strnlen_user)
