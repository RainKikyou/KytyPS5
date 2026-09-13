# Demon's Souls rendering profile

This separate change depends on the portable rendering optimizations. It applies
only to title `PPSA01341`, application version `01.007.000`. Other versions and
titles keep the normal synchronization and shader paths. There are no environment
variables or machine-specific configuration files required by this profile.

## Compute boundaries

The checked guest command stream marks dependent compute groups with explicit
synchronization. Within such a group, resource preparation still runs for each
dispatch, but a pending compute dependency may be deferred across consecutive
dispatches. This is a **title-specific assumption**, not proof that arbitrary
consecutive Vulkan dispatches are independent.

Uploads, transfers, draws, host commands, natural submissions and guest
ACQUIRE_MEM, WAIT_REG_MEM, end-of-pipe or synchronization events drain the pending
dependency. Full guest barriers subsume it. Indirect arguments are resolved before
selecting the command buffer or checking the pending dependency. The generic
bounded submission path drains the dependency before submitting as well.

The GPU regression test checks independent writes, RAW/WAR dependencies, GPU
produced indirect counts, transfer boundaries and asynchronous submissions against
all output words. It also checks isolation of unknown titles and versions. These
fixtures do not constitute a full-game compatibility test.

## Linear subset of the periodic-copy kernel

For shader hash `eb7456322124ecc7`, the verified behavior is
`dst[i] = src[i % period]` for `i < count`. Only `period >= count` is lowered to
the existing coherent `BufferCache::CopyBuffer` implementation. The guards require
exact resource formats, addressing controls, dispatch and workgroup shape,
matching bounds, no overlap or physical alias, and CPU-readable clean controls.
All other cases use the original shader. No game shader bytes are included.

This small specialization is kept separate from general shader analysis. A future
IR-based proof could generalize it; the current hash recognizer makes no such claim.

## Idle job polling

The verified empty-job path now waits for 50 microseconds using the host's blocking
sleep API. The generic short-sleep API intentionally spins at this duration, which
would keep idle guest workers competing with the graphics thread. No host CPU
number, affinity mask, or operating-system syscall is embedded in the patch.

Installation also requires the main executable's call-site and poll-prologue byte
signatures. An owned nearby executable allocation holds a thunk that calls the
original poll and waits only when its boolean result is false. It preserves the
return value, caller-saved general registers and legacy FP/XMM state. A mismatched
signature or unavailable allocation leaves the original guest path active.

## Coherent bulk memory copies

The shared memmove/bcopy entry in the supported libc module has been inspected at
the instruction level. Its argument/stack checks and bcopy argument swap execute
before the replacement entry. The adapter removes the single saved RBP and tail
calls a host System V ABI wrapper; ordinary host ABI bridging handles Windows.

A title/version check and a hash of the complete audited 2 KiB function body are
required before installation. Different modules, relocations or platform patches
are rejected. No game executable or shader data is shipped in this change.

Copies of at least 256 KiB prepare the complete destination through the existing
coherent invalidation API. GPU-written bytes and partial-page neighbours retain
normal synchronization; the copy itself still uses the guest mapping so later
protection changes and source reads follow the production fault path. Smaller,
zero-length, same-address and overlapping copies retain memmove semantics. There
are no user tuning switches. This is a separate title-specific replacement, not a
general recognition rule for every game's copy routines.

Both loader patches are installed before module initializers run and restore only
their own unchanged patch bytes before module teardown. Their machine-code adapters
are restricted to x86-64; other architectures retain the original guest code.
