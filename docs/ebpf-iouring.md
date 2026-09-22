# eBPF and io_uring

A hose round trip on the loopback is about 16 µs. A coroutine switch is
about 33 ns. The interesting gap is the socket and the wake, not the
allocator, and not the time io_uring spends finding a file descriptor.
Anything that adds a copy, a syscall, or a capability check has to beat
16 µs. Beating 33 ns is a hobby.

This note is what was measured on the machine the library is developed on:
Linux 5.15.148, libbpf 0.5, liburing 2.1. The conclusion is short. The
designs that do not survive contact with that machine are listed so they
do not have to be reinvented.

## What the ring is already not doing

Kernels before 5.19 are not used for io_uring. 5.15 falls back to `poll`
at init. A BPF program attached to a ring that is not open does not become
more clever by being attached. It becomes a program attached to nothing.

`SUSPENDERS_IOURING_SQPOLL` was measured 40% slower on this board: 22.4 µs
against 16.0 µs for a 64-byte TCP round trip. The kernel polling thread
takes a core the workers wanted. The flag stays off. BPF does not give the
core back.

## Designs that exist, and why they lose here

**A BPF program as a submission-queue entry.** There is no stable operation
that runs BPF and then resumes a coroutine with a buffer. `IORING_OP_URING_CMD`
is how nvme and ublk talk to their drivers. It is not a `recv`. Loading the
program also wants `CAP_BPF` or root. libbpf 0.5 can load a reuseport
program. It cannot express the helpers the newer write-ups assume. Those
write-ups are about a different kernel than the one in the machine.

**Registered buffers.** `IORING_REGISTER_BUFFERS` has existed since early
io_uring. It skips `get_user_pages` on a buffer the ring already knows.
The hose call is `read` into the caller's memory. A registered bounce
buffer is a memcpy on the way in and again on the way out. For the sizes
a hose moves, the copy is larger than the pin it avoids. Provided buffer
rings and multishot receive arrived in 5.19, which this kernel does not
have, and they want the library to own the buffers. That is a different
API. We did not change the API to win a benchmark we had not lost yet.

**AF_XDP in front of the ring.** XDP redirects frames to an `AF_XDP`
socket. io_uring completes the fill and completion queues. The result is
fast and it is no longer a socket, a hose, or a coroutine that calls
`read`. It is a second I/O stack. The unit of work here is a function that
blocks in `read` and keeps its locals. Replacing that to save microseconds
you then spend reconstructing the stack is a trade with one side missing.

## The design that would earn its keep

What grows with workers is not the ring. It is one listen socket. One
coroutine accepts. The connection is then pinned to whichever worker drew
it, and the bytes cross an inbox and a wake. Steering the packet before
userspace sees it removes that hop.

On 5.15 the tool is `SO_ATTACH_REUSEPORT_EBPF`
(`BPF_PROG_TYPE_SK_REUSEPORT`, in the kernel since 4.6):

- Each worker binds a UDP socket to the same port with `SO_REUSEPORT`.
- The program hashes the QUIC destination connection id and returns that
  worker's socket.
- Only that worker's ring, or its poll set, wakes.

A long-header Initial carries the destination id at byte 6 for 8 bytes,
which is the length this stack uses. A short header carries it at byte 1.
The program is not in the tree. It needs `CAP_BPF`, one socket per worker,
and a connection-id map the current listener does not have. Sketch, so the
next person does not start from a blank page:

```c
/* SEC("sk_reuseport"): return the bucket for this packet. */
int steer(struct sk_reuseport_md *md) {
    uint8_t dcid[8];
    if (bpf_skb_load_bytes(md, 6, dcid, sizeof(dcid)) < 0)
        return 0;
    return dcid[0] % /* worker count */;
}
```

TCP is the same shape with `sk_lookup` (5.9 or later) or a reuseport
program keyed by the 4-tuple. Same privilege, same "one socket per worker"
change. Build it when a profile says accept fan-in is the bottleneck.
Until then it is a program that would have been loaded by a process that
is not allowed to load it, in front of a cost that has not shown up.

## What was done instead

Memento 3's per-thread cache is the allocator hot path. `malloc_trim` no
longer discards the header of a live span. `memento_heap_release_caches`
unmaps a span only when the user holds nothing in it, and Suspenders calls
that when a worker exits and at shutdown.

`MADV_COLLAPSE` is attempted once per span. The first `EINVAL` or `ENOSYS`
— Linux before 6.1 — turns it off. It used to be a failing syscall on
every idle flush, which is a novel way to spend the time you meant to save.

QUIC uses the UDP path the hoses already had. No BPF.

Come back to registered buffers if a profile shows `get_user_pages` on a
hose that reuses its buffers. Come back to reuseport BPF when a
multi-worker accept test is inbox-bound and the process may load BPF.
Until one of those is true, the 16 µs is the number to beat, and it is not
beaten by a program that does not run.
