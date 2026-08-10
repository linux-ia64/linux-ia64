// SPDX-License-Identifier: GPL-2.0
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include "../disasm.h"

/*
 * ia64 instructions are issued in 128-bit bundles of three slots.  objdump
 * gives every slot its own address (bundle + 0/6/12), so the offsets perf
 * computes from the disassembly are fine.  Sampled IPs, however, are always
 * bundle addresses -- the slot number lives in the ri field of cr.ipsr, which
 * perf does not record -- so all of a bundle's samples are attributed to its
 * slot 0 line.
 */

/*
 * Mnemonics are dotted and carry completers, e.g. "br.call.sptk.many" or
 * "nop.m".  Match on the dot-delimited stem instead of the whole name.
 */
static bool ia64_ins__is(const char *name, const char *stem)
{
	size_t len = strlen(stem);

	return !strncmp(name, stem, len) && (name[len] == '\0' || name[len] == '.');
}

/* A branch register operand, i.e. an indirect target: "b0", "b7;;", ... */
static bool ia64_is_branch_reg(const char *s)
{
	if (s[0] != 'b' || !isdigit(s[1]))
		return false;

	s += 2;
	while (isdigit(*s))
		s++;

	return *s == '\0' || *s == ';' || isspace(*s);
}

/*
 * Calls name the branch register that receives the return address:
 *
 *	br.call.sptk.many b0=a0000001007f0040 <__fsnotify_parent>;;
 *	br.call.sptk.many b0=b7;;
 *
 * Skip the "bN=" part, otherwise call__parse() reads "b0=..." as the address
 * 0xb0.  ops->raw points into dl->al.line and is not separately owned, so
 * advancing it is safe and the full text is still displayed.
 */
static int ia64_call__parse(const struct arch *arch, struct ins_operands *ops,
			    struct map_symbol *ms, struct disasm_line *dl)
{
	char *s = ops->raw;

	if (s[0] == 'b' && isdigit(s[1])) {
		s = strchr(s, '=');
		if (s == NULL)
			return -1;
		s++;
	}

	/* Indirect call: no target we can resolve, but still a call. */
	if (ia64_is_branch_reg(s)) {
		ops->target.addr = 0;
		return 0;
	}

	ops->raw = s;
	return call__parse(arch, ops, ms, dl);
}

static const struct ins_ops ia64_call_ops = {
	.parse	   = ia64_call__parse,
	.scnprintf = call__scnprintf,
	.is_call   = true,
};

/* "br.cond.sptk.many b6" -- indirect, leave it as a plain instruction. */
static int ia64_jump__parse(const struct arch *arch, struct ins_operands *ops,
			    struct map_symbol *ms, struct disasm_line *dl)
{
	if (ia64_is_branch_reg(ops->raw))
		return -1;

	return jump__parse(arch, ops, ms, dl);
}

static const struct ins_ops ia64_jump_ops = {
	.free	   = jump__delete,
	.parse	   = ia64_jump__parse,
	.scnprintf = jump__scnprintf,
	.is_jump   = true,
};

static const struct ins_ops *ia64__associate_ins_ops(struct arch *arch,
						     const char *name)
{
	const struct ins_ops *ops = NULL;

	/* catch function call */
	if (ia64_ins__is(name, "br.call") || ia64_ins__is(name, "brl.call"))
		ops = &ia64_call_ops;
	/* catch function return */
	else if (ia64_ins__is(name, "br.ret"))
		ops = &ret_ops;
	/*
	 * catch all kind of jumps: br.cond, br.few, br.many, br.sptk,
	 * br.spnt, br.dptk, br.dpnt, the counted and modulo-scheduled loop
	 * branches br.cloop/br.ctop/br.cexit/br.wtop/br.wexit, br.ia, brl.cond
	 * and the speculation checks chk.s/chk.a, which branch to recovery code.
	 */
	else if (ia64_ins__is(name, "br") || ia64_ins__is(name, "brl") ||
		 ia64_ins__is(name, "chk"))
		ops = &ia64_jump_ops;
	/* nop.m, nop.i, nop.b, nop.f, nop.x */
	else if (ia64_ins__is(name, "nop"))
		ops = &nop_ops;

	if (ops)
		arch__associate_ins_ops(arch, name, ops);
	return ops;
}

const struct arch *arch__new_ia64(const struct e_machine_and_e_flags *id,
				  const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "ia64";
	arch->id = *id;
	arch->objdump.comment_char = '/';	/* ia64 comments start with "//" */
	arch->associate_instruction_ops = ia64__associate_ins_ops;
	return arch;
}
