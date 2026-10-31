.. SPDX-License-Identifier: GPL-2.0
.. _ia64_perf:

============================
Performance events on IA-64
============================

Introduction
============

IA-64 uses the generic perf_events interface.  Software events and tracepoints
are available on every IA-64 processor.  Hardware events are provided by the
IA-64 PMU driver when ``CONFIG_PERF_EVENTS`` is enabled.

The driver uses the counting monitors, PMC/PMD4 and up.  The other facilities
of the IA-64 PMU -- the event address registers, the branch trace buffer, the
opcode match registers and address range checking -- do not map onto a perf
counter and are not driven by the driver.  The old IA-64 specific perfmon
interface that used them has been removed; see `Relationship to perfmon`_.

Supported PMUs
==============

The driver selects a PMU description from the processor family reported in
CPUID[3].  The known implementations are:

=====================  =======  ==================  =============
Model                  Family   Counting monitors   Counter width
=====================  =======  ==================  =============
Itanium ("Merced")     0x7      4 (PMD4-PMD7)       32 bits
Itanium 2              0x1f     4 (PMD4-PMD7)       47 bits
Montecito/Montvale     0x20     12 (PMD4-PMD15)     47 bits
=====================  =======  ==================  =============

Itanium has a 7 bit event select field; bit 15 of its PMC is reserved.
Itanium 2 has an 8 bit event select field and requires the enable bit in PMC4
to be set for any counter to run.  Montecito requires the instruction set mask
to select IA-64 mode, has a bit selecting whether a monitor counts for one
thread or both, and has a MESI qualifier for the L2D and L3 events that
support it.

If the processor family is not known to the driver, a generic architected
description is built from ``PAL_PERF_MON_INFO``.  It provides the number of
counting monitors, their width, and the architected cycle and
retired-instruction event codes.  Only those two events can be used on such a
processor.

The information reported by ``PAL_PERF_MON_INFO`` is available in
``/proc/pal/perfmon_info`` when ``CONFIG_IA64_PALINFO`` is enabled.  It is
useful for checking that the driver has selected the right description for a
processor.

Generic hardware events
=======================

The usual ``PERF_TYPE_HARDWARE`` aliases are mapped to model specific event
codes.  An alias that a model cannot count fails with ``EOPNOTSUPP``.

=========================  ============  ============  ============
perf event                 Itanium       Itanium 2     Montecito
=========================  ============  ============  ============
``cycles``                 yes           yes           yes
``instructions``           yes [#ita]_   yes           yes
``cache-references``       yes           yes           yes
``cache-misses``           yes           yes           yes
``branch-instructions``    yes           yes           yes
``branch-misses``          yes           yes           yes
=========================  ============  ============  ============

.. [#ita] On Itanium, retired instructions can only be counted on PMD4 and
   PMD5.

The ``PERF_TYPE_HW_CACHE`` events support read accesses and read misses for
L1D, L1I, the last level cache, DTLB, ITLB and the branch prediction unit.
L1I prefetch accesses are supported on Itanium 2 and Montecito.  Other cache
event combinations are not implemented.

The exact event codes and the counters able to count each event are listed in
``arch/ia64/kernel/perf_event_ita.h``, ``perf_event_mck.h`` and
``perf_event_mont.h``.  These tables use the libpfm encoding: the event select
is in bits 7..0 and the unit mask in bits 19..16.

Raw events
==========

PMU events that are not covered by a generic alias can be selected with a raw
event.  A raw config is laid out like the x86 one, not like the value written
to a PMC:

=========  ============  =========================
Field      Config bits   PMC field
=========  ============  =========================
``event``  7..0          event select, bits 15..8
``umask``  11..8         unit mask, bits 19..16
``thres``  14..12        threshold, bits 22..20
``all``    16            both threads, bit 26
``mesi``   20..17        MESI qualifier, bits 30..27
=========  ============  =========================

The fields are also described in sysfs under
``/sys/bus/event_source/devices/cpu/format/``.  ``all`` and ``mesi`` only
exist on Montecito; on the other models those bits are rejected.  Bits outside
the layout, or fields the selected model does not implement, cause the event
to be rejected with ``EINVAL`` instead of silently counting something else.

For example, event 0x12 is CPU cycles on all of the known implementations::

  perf stat -e r12
  perf stat -e cpu/event=0x12/

A unit mask is added in bits 11..8, so event 0x02 with unit mask 1 is
``r102``.  On Montecito, ``mesi`` is a four-bit qualifier for the events that
implement MESI state filtering; an empty qualifier selects no state for those
events.  Not every event uses the field.  The generic cache aliases carry all
four states in ``mesi``, and the raw form of the cache-reference alias can be
written explicitly::

  perf stat -e cpu/event=0xdb,mesi=0xf/

The raw config is not the value of a PMC register.  The old PMC-shaped
spellings such as ``r1200`` for CPU cycles and ``r10200`` for event 0x02 with
unit mask 1 no longer mean the same thing; use ``r12`` and ``r102``.

No named model-specific events are exported by the driver, so ``perf list``
does not show them.  Use the raw encoding, the generic aliases above, or the
event names and codes from libpfm.

Counting modes
==============

The ``exclude_kernel`` and ``exclude_user`` attributes control whether a
counter counts at IA-64 privilege level 0 (kernel) or privilege level 3
(user).  Both are counted by default.  Excluding both is invalid.  The
``exclude_idle`` attribute is not supported, and ``exclude_hv``,
``exclude_host`` and ``exclude_guest`` have no effect because the driver does
not model a hypervisor privilege level.

The monitors are programmed as privileged (``pmc.pm``), which gates them on
``psr.pp`` rather than ``psr.up``.  A monitored task therefore cannot turn
counting off behind perf's back.  Every interruption restores ``psr.pp`` from
``dcr.pp``, which the driver sets once per CPU, so kernel code is counted
without a hook on every kernel entry path.  User level needs the saved
``psr.pp`` of the incoming task to be fixed up on a context switch; this is
done only while at least one monitor on the CPU counts user level code.

Sampling and overflows
======================

Each counter is armed with ``pmc.oi``.  When it overflows, the PMU freezes,
records the overflow in PMC0 and raises the performance monitor interrupt
vector.  The handler folds the counter into the event, re-arms it and clears
the overflow status.

Counting events are also armed with an overflow period, half of the counter
range, so that short counters are folded before they can wrap unnoticed.  This
does not generate samples.  A sampling event's period is clamped to half the
counter range for the same reason.

A counter must be armed with at least two events left.  Arming it with a
single event does not work on the Montecito PMU: it delivers one overflow and
then no more until something reloads it.  The driver therefore arms for a
minimum of two events, which also keeps the default frequency mode of
``perf record`` working.  The period reported with a sample remains the period
perf asked for.

The PMU has no NMI support; overflows are delivered as ordinary maskable
interrupts.

Limitations
===========

* Only the counting monitors are supported.  There is no support for event
  address registers, the branch trace buffer, opcode matching, address range
  checking or the IA-64 debug registers through perf.

* Branch stack sampling is not supported, and ``exclude_idle`` is rejected.

* The Itanium 2 rule that events of the L2 cache group share the unit mask
  programmed into PMC4 is not enforced.  Two conflicting events from that
  group scheduled together will miscount rather than be rejected.

* On Montecito, the MESI qualifier only affects the events that implement it.
  The generic cache aliases carry all four states in ``mesi``; a raw event
  that needs MESI filtering has to select its states explicitly.

* The generic fallback description obtained from ``PAL_PERF_MON_INFO`` only
  knows CPU cycles and retired instructions.

Relationship to perfmon
=======================

IA-64 used to have its own perfmon interface, ``perfmonctl(2)``, with its own
sampling buffer and support for the event address registers, branch trace
buffer, opcode matchers and address range checking.  It was removed as broken
in commit ecf5b72d5f66 ("ia64: Remove perfmon"), and the remaining pieces,
including the old oprofile support, were removed later.  Nothing of that
interface remains; perf_events is now the supported interface.

The perfmon name survives in two places: the performance monitor interrupt
source is still registered as ``perfmon``, and ``/proc/pal/perfmon_info``
reports the PMU information obtained from PAL.

Further reading
===============

* ``arch/ia64/kernel/perf_event.c`` -- the PMU driver.
* ``arch/ia64/kernel/perf_event_gen.h``, ``perf_event_ita.h``,
  ``perf_event_mck.h`` and ``perf_event_mont.h`` -- the PMU descriptions.
* ``arch/ia64/include/asm/perf_event.h`` -- the architected PMC bits.
* Intel Itanium Architecture Software Developer's Manual, volume 2,
  "Performance Monitoring".
* Documentation/admin-guide/perf-security.rst -- perf access control.
