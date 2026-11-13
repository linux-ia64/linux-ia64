.. SPDX-License-Identifier: GPL-2.0

=====================================
Deferring speculative data TLB misses
=====================================

A control-speculative load (``ld.s``, ``ldf.s``, ``lfetch`` without
``.fault``) does not have to raise its faults: the processor can *defer* a
fault by setting the target's NaT bit, and the program's ``chk.s`` branches
to recovery code that redoes the load non-speculatively.  Which fault
classes are deferred is set by the ``d*`` bits of ``cr.dcr`` (SDM vol. 2,
"Deferral of Speculative Load Faults").

``cpu_init()`` sets every deferral bit except ``dcr.dm``: page not present,
key miss/permission, access rights, access bit and debug faults are
deferred, data TLB misses are not.  With late deferral (the Itanium 2
default) a speculative load that misses the TLB starts a VHPT walk; when the
walk finds the PTE, the load completes, and when the PTE is not present the
page-not-present fault is deferred.  But when the walk itself fails because
there is no page-table page for the address (nothing mapped anywhere near
it), the load takes a Data TLB fault.  Linux handles it in
``ia64_do_page_fault()``, finds no VMA, and in ``bad_area`` sets ``psr.ed``
so that the load is restarted and returns a NaT.  The program sees the NaT
it would have got anyway, after a trip through the page-fault path that is
counted as a page fault.

Code that speculates loads above the branches that make them safe, as a JIT
does when it hoists a load above a loop exit or a type guard, can produce
such addresses on a path it takes all the time.  For such code a thread can
ask the kernel to run it with ``dcr.dm`` set.

Interface
=========

``prctl(PR_IA64_SET_SPEC_DEFER, ctl, 0, 0, 0)``
	Set the fault classes this thread's speculative loads defer beyond
	the default.  ``ctl`` is a mask of:

	``PR_IA64_SPEC_DEFER_TLB``
		Defer data TLB misses that the VHPT walker cannot resolve
		(``dcr.dm``).

	``0`` restores the default.  Unknown bits and non-zero unused
	arguments give ``EINVAL``.

``prctl(PR_IA64_GET_SPEC_DEFER, 0, 0, 0, 0)``
	Return the current mask.

The setting is per thread.  It is inherited by threads and processes
created with ``clone()``/``fork()`` and kept across ``execve()``, so a
wrapper can turn it on for an unmodified program.  Set it early, before a
program starts its threads.

Effect
======

Every correct IA-64 program already handles a NaT from any speculative
load: the architecture lets the processor defer at any time.  The setting
changes cost, not behavior:

- A speculative load from an address with no page table behind it returns
  its NaT without entering the kernel.
- A speculative load from a valid page that the walker cannot resolve
  (its VHPT translation is not in the TLB) also returns a NaT instead of
  being filled in, so the program pays for its recovery path, whose
  ordinary load then takes the TLB miss.

Implementation
==============

``cr.dcr`` is per CPU.  The thread flag ``IA64_THREAD_SPEC_DEFER_TLB``
records the setting, and ``dcr.dm`` on each CPU always matches the flag of
the task running there: CPUs come up with ``dm`` clear under a task without
the flag, ``__switch_to()`` rewrites ``dcr`` only when the flag differs
between the outgoing and incoming tasks, and the ``prctl()`` changes flag
and register together with interrupts disabled.  ``dcr.dm`` stays set while
such a thread runs in the kernel.  The kernel does not depend on the
deferral setting: its own speculative loads (``strlen()``,
``strnlen_user()``, and any the compiler emits) all have recovery code.
