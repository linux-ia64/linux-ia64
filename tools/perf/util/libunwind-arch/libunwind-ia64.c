// SPDX-License-Identifier: GPL-2.0
/*
 * ia64 user unwinding with libunwind-ia64, driven by the sample's registers
 * (PERF_SAMPLE_REGS_USER) and its user stack dump (PERF_SAMPLE_STACK_USER,
 * see struct perf_ia64_ustack_hdr in the uapi perf_regs.h).
 *
 * ia64 has no frame-pointer or DWARF unwind info in perf's generic paths.
 * Unwind info is the PT_IA_64_UNWIND table in the DSO, which this file reads
 * from the ELF file and hands to libunwind's remote table search.
 */
#include "libunwind-arch.h"
#include "../debug.h"
#include "../dso.h"
#include "../map.h"
#include "../maps.h"
#include "../sample.h"
#include "../symbol.h"
#include "../thread.h"
#include "../perf_regs.h"
#include "../../../arch/ia64/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <elf.h>
#include <errno.h>
#include <gelf.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
#include <libunwind-ia64.h>
#endif

/* Kernel text starts at GATE_ADDR + 4 GiB (asm/page.h). */
#define IA64_KERNEL_START	0xa000000100000000ULL

/* A call bundle is 16 bytes; its slot 2 is where a return address's call sits. */
#define IA64_BUNDLE_MASK	(~0xfULL)

/*
 * Register-stack address arithmetic (the kernel's rse.h, reimplemented here
 * on purpose).  Skips num_regs stacked registers from addr, stepping over the
 * RSE NaT collection slot (every 64th slot, slot 0x3f).
 */
static u64 ia64_rse_skip_regs_u64(u64 addr, long num_regs)
{
	long delta = (long)((addr >> 3) & 0x3f) + num_regs;

	if (num_regs < 0)
		delta -= 0x3e;
	return addr + ((num_regs + delta / 0x3f) << 3);
}

struct ia64_unw_info {
	u64 segbase;		/* text segment base: start of the table's ip range */
	u64 text_len;		/* text segment size */
	u64 table_addr;		/* PT_IA_64_UNWIND table, in the process address space */
	u64 table_len;		/* table entries, in unw_word_t words */
};

int __get_perf_regnum_for_unw_regnum_ia64(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_IA64_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_IA64_GR ... UNW_IA64_GR + 31:
		/* Stacked registers (r32 and up) come from the RBS, not here. */
		return unw_regnum - UNW_IA64_GR + PERF_REG_IA64_R0;
	case UNW_IA64_BR ... UNW_IA64_BR + 7:
		return unw_regnum - UNW_IA64_BR + PERF_REG_IA64_B0;
	case UNW_IA64_IP:
		return PERF_REG_IA64_IP;
	case UNW_IA64_CFM:
		return PERF_REG_IA64_CFM;
	case UNW_IA64_PR:
		return PERF_REG_IA64_PR;
	/*
	 * After a stop the dirty partition is in user memory, so the RBS
	 * counts as fully spilled: bspstore is the after-cover BSP, which is
	 * what libunwind's RBS area needs (see access_reg for the frame BSP).
	 */
	case UNW_IA64_AR_BSP:
	case UNW_IA64_AR_BSPSTORE:
		return PERF_REG_IA64_BSP;
	case UNW_IA64_AR_PFS:
		return PERF_REG_IA64_PFS;
	case UNW_IA64_AR_RNAT:
		return PERF_REG_IA64_RNAT;
	case UNW_IA64_AR_UNAT:
		return PERF_REG_IA64_UNAT;
	case UNW_IA64_AR_RSC:
		return PERF_REG_IA64_RSC;
	case UNW_IA64_AR_FPSR:
		return PERF_REG_IA64_FPSR;
	case UNW_IA64_AR_CCV:
		return PERF_REG_IA64_CCV;
	default:
		return -EINVAL;
	}
#endif
}

void __libunwind_arch__flush_access_ia64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_ia64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}

#ifdef HAVE_LIBUNWIND_IA64_SUPPORT

/*
 * Read the unwind table location for the DSO that contains a map.  Table and
 * segment addresses are converted to process addresses with the load bias,
 * found from the PT_LOAD that the map covers (same approach as the generic
 * eh_frame code).
 */
static int read_ia64_unwind_info(struct dso *dso, struct machine *machine,
				 struct map *map, struct ia64_unw_info *info)
{
	u64 map_pgoff = map__pgoff(map);
	u64 psz = (u64)getpagesize();
	u64 text_vaddr = 0, unw_vaddr = 0, unw_memsz = 0, bias = 0;
	bool have_text = false, have_unw = false, have_bias = false;
	Elf *elf;
	GElf_Phdr phdr;
	size_t i, phdrnum = 0;
	int fd, ret = -EINVAL;

	if (!dso__data_get_fd(dso, machine, &fd))
		return -EINVAL;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (!elf)
		goto out_put;

	(void)elf_getphdrnum(elf, &phdrnum);
	for (i = 0; i < phdrnum; i++) {
		if (!gelf_getphdr(elf, i, &phdr))
			continue;
		if (phdr.p_type == PT_LOAD && (phdr.p_flags & PF_X) && !have_text) {
			text_vaddr = phdr.p_vaddr;
			info->text_len = phdr.p_memsz;
			have_text = true;
		}
		if (phdr.p_type == PT_IA_64_UNWIND) {
			unw_vaddr = phdr.p_vaddr;
			unw_memsz = phdr.p_memsz;
			have_unw = true;
		}
	}

	/* The map that starts the bias: the PT_LOAD at this map's file offset. */
	for (i = 0; i < phdrnum && !have_bias; i++) {
		if (!gelf_getphdr(elf, i, &phdr))
			continue;
		if (phdr.p_type == PT_LOAD &&
		    (phdr.p_offset & ~(psz - 1)) == map_pgoff) {
			bias = map__start(map) - (phdr.p_vaddr & ~(psz - 1));
			have_bias = true;
		}
	}

	elf_end(elf);
	if (have_text && have_unw && have_bias) {
		info->segbase = bias + text_vaddr;
		info->table_addr = bias + unw_vaddr;
		info->table_len = unw_memsz / sizeof(unw_word_t);
		ret = 0;
	}
out_put:
	dso__data_put_fd(dso);
	return ret;
}

static int find_proc_info(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t *pi,
			  int need_unwind_info, void *arg)
{
	struct unwind_info *ui = arg;
	struct ia64_unw_info info;
	unw_dyn_info_t di;
	struct map *map;
	struct dso *dso;
	int ret;

	map = __libunwind__find_map(ip, ui);
	if (!map)
		return -UNW_ENOINFO;

	dso = map__dso(map);
	if (!dso) {
		map__put(map);
		return -UNW_ENOINFO;
	}

	memset(&info, 0, sizeof(info));
	ret = read_ia64_unwind_info(dso, ui->machine, map, &info);
	if (ret || ip < info.segbase || ip >= info.segbase + info.text_len) {
		ret = -UNW_ENOINFO;
		goto out;
	}

	memset(&di, 0, sizeof(di));
	di.format = UNW_INFO_FORMAT_REMOTE_TABLE;
	di.start_ip = info.segbase;
	di.end_ip = info.segbase + info.text_len;
	di.gp = 0;
	di.u.rti.segbase = info.segbase;
	di.u.rti.table_data = info.table_addr;
	di.u.rti.table_len = info.table_len;

	ret = unw_search_ia64_unwind_table(as, ip, &di, pi, need_unwind_info, arg);
out:
	map__put(map);
	return ret;
}

/* libunwind mallocs pi->unwind_info for a non-local address space. */
static void put_unwind_info(unw_addr_space_t __maybe_unused as,
			    unw_proc_info_t *pi, void __maybe_unused *arg)
{
	free((void *)(uintptr_t)pi->unwind_info);
	pi->unwind_info = 0;
}

static int get_dyn_info_list_addr(unw_addr_space_t __maybe_unused as,
				  unw_word_t __maybe_unused *dil_addr,
				  void __maybe_unused *arg)
{
	return -UNW_ENOINFO;
}

/*
 * Memory reads.  The dump holds the RBS image and the memory stack, each
 * a contiguous range of the user address space.  Anything else is read from
 * the DSO file.
 */
static int access_mem(unw_addr_space_t as __maybe_unused, unw_word_t addr,
		      unw_word_t *valp, int write, void *arg)
{
	struct unwind_info *ui = arg;
	struct stack_dump *stack = &ui->sample->user_stack;
	struct perf_ia64_ustack_hdr hdr;
	int ret;

	if (write) {
		*valp = 0;
		return 0;
	}

	if (stack && stack->data && stack->size >= sizeof(hdr)) {
		memcpy(&hdr, stack->data, sizeof(hdr));
		if (hdr.magic == PERF_IA64_USTACK_MAGIC &&
		    sizeof(hdr) + hdr.rbs_len + hdr.mem_len <= stack->size) {
			u64 off;

			if (addr >= hdr.rbs_addr &&
			    addr + sizeof(*valp) <= hdr.rbs_addr + hdr.rbs_len) {
				off = sizeof(hdr) + (addr - hdr.rbs_addr);
				memcpy(valp, &stack->data[off], sizeof(*valp));
				return 0;
			}
			if (addr >= hdr.mem_addr &&
			    addr + sizeof(*valp) <= hdr.mem_addr + hdr.mem_len) {
				off = sizeof(hdr) + hdr.rbs_len + (addr - hdr.mem_addr);
				memcpy(valp, &stack->data[off], sizeof(*valp));
				return 0;
			}
		}
	}

	ret = __libunwind__access_dso_mem(ui, addr, valp);
	if (ret) {
		*valp = 0;
		return -UNW_EINVAL;
	}
	return 0;
}

/*
 * Register reads.  The top frame's BSP is the frame start: libunwind wants
 * skip(BSP, -sof), where BSP is the after-cover value that the sample holds.
 */
static int access_reg(unw_addr_space_t as __maybe_unused, unw_regnum_t regnum,
		      unw_word_t *valp, int write, void *arg)
{
	struct unwind_info *ui = arg;
	u64 val, cfm, bsp;
	int id, ret;

	if (write) {
		pr_err("unwind: access_reg write %d unsupported\n", regnum);
		return -UNW_EINVAL;
	}

	if (!ui->sample->user_regs || !ui->sample->user_regs->regs) {
		*valp = 0;
		return 0;
	}

	/* NaT collection registers are not in the sample: they read as 0. */
	if (regnum >= UNW_IA64_NAT && regnum < UNW_IA64_NAT + 128) {
		*valp = 0;
		return 0;
	}

	if (regnum == UNW_IA64_AR_BSP) {
		ret = perf_reg_value(&bsp, perf_sample__user_regs(ui->sample),
				     PERF_REG_IA64_BSP);
		if (ret)
			return -UNW_EBADREG;
		ret = perf_reg_value(&cfm, perf_sample__user_regs(ui->sample),
				     PERF_REG_IA64_CFM);
		if (ret)
			return -UNW_EBADREG;
		*valp = (unw_word_t)ia64_rse_skip_regs_u64(bsp, -(long)(cfm & 0x7f));
		return 0;
	}

	id = __get_perf_regnum_for_unw_regnum_ia64(regnum);
	if (id < 0)
		return -UNW_EBADREG;

	/*
	 * A kernel-mode sample inside an fsys fast syscall has its IP in
	 * kernel text, which libunwind has no info for.  The fsys handlers
	 * keep the user return address in b6, so start from there.
	 */
	if (regnum == UNW_IA64_IP) {
		ret = perf_reg_value(&val, perf_sample__user_regs(ui->sample),
				     PERF_REG_IA64_IP);
		if (!ret && val >= IA64_KERNEL_START) {
			ret = perf_reg_value(&val, perf_sample__user_regs(ui->sample),
					     PERF_REG_IA64_B6);
			if (ret)
				return -UNW_EBADREG;
			*valp = val;
			return 0;
		}
	}

	ret = perf_reg_value(&val, perf_sample__user_regs(ui->sample), id);
	if (ret) {
		if (!ui->best_effort)
			pr_err("unwind: can't read reg %d\n", regnum);
		return -UNW_EBADREG;
	}
	*valp = val;
	return 0;
}

static int access_fpreg(unw_addr_space_t __maybe_unused as,
			unw_regnum_t __maybe_unused num,
			unw_fpreg_t __maybe_unused *val,
			int __maybe_unused write,
			void __maybe_unused *arg)
{
	pr_err("unwind: access_fpreg unsupported\n");
	return -UNW_EINVAL;
}

static int resume(unw_addr_space_t __maybe_unused as,
		  unw_cursor_t __maybe_unused *cu,
		  void __maybe_unused *arg)
{
	pr_err("unwind: resume unsupported\n");
	return -UNW_EINVAL;
}

static int get_proc_name(unw_addr_space_t __maybe_unused as,
			 unw_word_t __maybe_unused addr,
			 char __maybe_unused *bufp, size_t __maybe_unused buf_len,
			 unw_word_t __maybe_unused *offp, void __maybe_unused *arg)
{
	pr_err("unwind: get_proc_name unsupported\n");
	return -UNW_EINVAL;
}
#endif /* HAVE_LIBUNWIND_IA64_SUPPORT */

void *__libunwind_arch__create_addr_space_ia64(void)
{
#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
	static unw_accessors_t accessors = {
		.find_proc_info		= find_proc_info,
		.put_unwind_info	= put_unwind_info,
		.get_dyn_info_list_addr	= get_dyn_info_list_addr,
		.access_mem		= access_mem,
		.access_reg		= access_reg,
		.access_fpreg		= access_fpreg,
		.resume			= resume,
		.get_proc_name		= get_proc_name,
	};

	return unw_create_addr_space(&accessors, /*byte_order=*/0);
#else
	return NULL;
#endif
}

struct unwind_info *__libunwind_arch_unwind_info__new_ia64(struct thread *thread __maybe_unused,
							   struct perf_sample *sample __maybe_unused,
							   int max_stack __maybe_unused,
							   bool best_effort __maybe_unused,
							   uint64_t first_ip __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
	struct arch_unwind_info {
		struct unwind_info ui;
		unw_cursor_t _cursor;
		uint64_t _ips[];
	};

	struct maps *maps = thread__maps(thread);
	void *addr_space = maps__addr_space(maps);
	struct arch_unwind_info *ui;
	int ret;

	if (addr_space == NULL)
		return NULL;

	ui = zalloc(sizeof(*ui) + sizeof(ui->_ips[0]) * max_stack);
	if (!ui)
		return NULL;

	ui->ui.machine = maps__machine(maps);
	ui->ui.thread = thread;
	ui->ui.sample = sample;
	ui->ui.cursor = &ui->_cursor;
	ui->ui.ips = &ui->_ips[0];
	ui->ui.ips[0] = first_ip;
	ui->ui.cur_ip = 1;
	ui->ui.max_ips = max_stack;
	ui->ui.unw_word_t_size = sizeof(unw_word_t);
	ui->ui.e_machine = EM_IA_64;
	ui->ui.best_effort = best_effort;

	ret = unw_init_remote(&ui->_cursor, addr_space, &ui->ui);
	if (ret) {
		if (!best_effort)
			pr_err("libunwind: %s\n", unw_strerror(ret));
		free(ui);
		return NULL;
	}

	return &ui->ui;
#else
	return NULL;
#endif
}

int __libunwind_arch__unwind_step_ia64(struct unwind_info *ui __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_IA64_SUPPORT
	int ret;

	if (ui->cur_ip >= ui->max_ips)
		return 0;

	ret = unw_step(ui->cursor);
	if (ret > 0) {
		uint64_t ip;

		unw_get_reg(ui->cursor, UNW_REG_IP, &ip);

		/*
		 * A caller frame's IP is the return address: the slot after the
		 * call.  Store the call's own slot so symbol and srcline lookups
		 * stay in the caller.  If the return slot is 1 or 2, the call is
		 * in the same bundle, one slot earlier.  If it is slot 0, the call
		 * is slot 2 of the previous bundle.  A signal frame's IP is exact.
		 */
		if (unw_is_signal_frame(ui->cursor) <= 0) {
			unsigned long ri = ip & 0x3;

			if (ri)
				ip = (ip & IA64_BUNDLE_MASK) | (ri - 1);
			else
				ip = ((ip & IA64_BUNDLE_MASK) - 0x10) | 2;
		}
		ui->ips[ui->cur_ip++] = ip;
	}
	return ret;
#else
	return -EINVAL;
#endif
}
