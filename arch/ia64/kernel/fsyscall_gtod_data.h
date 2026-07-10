/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *        Contributed by Peter Keilty <peter.keilty@hp.com>
 *
 * fsyscall gettimeofday data
 */

struct itc_jitter_data_t {
	int		itc_jitter;
	u64		itc_lastcycle;
} ____cacheline_aligned;

