// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>

/*
 * ia64 has no dwarf_search_unwind_table in libunwind-ia64, so probe the
 * entry points that perf's ia64 backend uses instead: address space and
 * remote cursor creation, and the ia64 unwind-table search.
 */
static unw_accessors_t accessors;

int main(void)
{
	unw_addr_space_t addr_space;

	addr_space = unw_create_addr_space(&accessors, 0);
	if (addr_space)
		return 0;

	unw_init_remote(NULL, addr_space, NULL);
	unw_search_ia64_unwind_table(addr_space, 0, NULL, NULL, 0, NULL);

	return 0;
}
