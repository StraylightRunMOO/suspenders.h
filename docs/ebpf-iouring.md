# eBPF and io_uring

What was looked at, on the machine this tree is developed on (Linux
5.15.148, libbpf 0.5, liburing 2.1), and what Suspenders should not pretend
to gain from it.

## What actually costs time here

A hose round trip on the loopback is about 16 µs. A coroutine switch is
about 33 ns. The gap is the socket operation and the suspend/wake, not the
allocator and not the fd-table lookup inside `io_uring_enter`. Anything
that adds a copy, a syscall, or a privilege check has to beat that 16 µs,
not the 33 ns.

`SUSPENDERS_IOURING_SQPOLL` was already measured 40% slower on this board
(a kernel thread per ring steals a core from the workers). It stays off.

Kernels before 5.19 are not used for io_uring at all. 5.15 falls back to
poll, which is what this Jetson runs. An eBPF program attached to a ring
that is not in use does nothing.

## Combinations that exist, and why they do not fit

**BPF inside the submission queue.** There is no stable operation that runs
a BPF program as an SQE and returns a userspace buffer to a suspended
coroutine. `IORING_OP_URING_CMD` is how nvme and ublk talk to the kernel,
not a packet path. Loading a program also needs `CAP_BPF` or root. libbpf
0.5 can load a reuseport program; it cannot express the 6.x helpers that
newer write-ups assume.

**Registered / provided buffers.** Fixed buffers (`IORING_REGISTER_BUFFERS`)
have been around since early io_uring, and they avoid `get_user_pages` on
a buffer the ring already knows. The hose API reads into the caller's
buffer. Using a registered bounce buffer means a memcpy on every read and
write. For the sizes hoses move, that copy is larger than the page-pin it
saves. Provided buffer rings (`io_uring_register_buf_ring`, multishot recv)
landed in 5.19, which this kernel does not have, and they want the server
to own the buffers. That fights `suspenders_hose_read(h, dest, len)`.

**AF_XDP plus io_uring.** A real zero-copy path: XDP redirects frames to an
`AF_XDP` socket and the ring completes the fill/completion queues. It
replaces the socket, the hose, and demux. It is a different I/O stack, not
a speedup of the one Suspenders has. Not worth it for a coroutine runtime
whose unit of work is a suspending `read`.

**SQPOLL plus BPF.** SQPOLL already lost on this hardware. BPF does not
change that.

## The combination that would pay, later

The cost that grows with workers is not the ring. It is a shared listen
socket: one coroutine accepts, and the connection is then pinned to
whichever worker drew it, so the data path crosses an inbox and a wake
fd. The fix is to steer the packet to the worker before userspace sees it.

On 5.15 the tool for that is `SO_ATTACH_REUSEPORT_EBPF`
(`BPF_PROG_TYPE_SK_REUSEPORT`, in the kernel since 4.6):

- Each worker owns a UDP socket bound to the same port with `SO_REUSEPORT`.
- The program hashes the QUIC destination connection id (bytes 6..13 of a
  long-header Initial, or the DCID of a short header once the length is
  known) and picks that worker's socket.
- That worker's io_uring (or poll set) is the only one that wakes.

Sketch, not shipped — it needs `CAP_BPF`, a socket per worker, and a CID
map the current single-socket `quic://` listener does not have:

```c
/* SEC("sk_reuseport") — return the reuseport bucket for this skb. */
int steer(struct sk_reuseport_md *md) {
    uint8_t dcid[8];
    /* Long header: DCID length is byte 5, DCID starts at byte 6. */
    if (bpf_skb_load_bytes(md, 6, dcid, 8) < 0)
        return 0;
    return dcid[0] % md->hash; /* or a SOCKARRAY lookup */
}
```

TCP has the same shape with `sk_lookup` (5.9+) or a reuseport program on
the listener, keyed by the 4-tuple instead of a CID. Same privilege and
same "one socket per worker" redesign. Do that when a workload is actually
bound on accept fan-in, not on the 16 µs loopback RTT.

## What landed instead

- Memento 3's per-thread tcache is the allocator hot path. `malloc_trim`
  no longer discards page headers of a live span (that abandoned the span
  and forced a new 2 MiB mapping). `memento_heap_release_caches` unmaps
  only spans with nothing in user hands, and Suspenders calls it when a
  worker exits and at shutdown.
- `MADV_COLLAPSE` runs at most once per span, and the first `EINVAL` from a
  kernel older than 6.1 disables it. It used to be a failing syscall on
  every idle flush.
- QUIC rides the existing UDP suspend path. No BPF.

Revisit registered buffers only if a profile shows `get_user_pages` inside
a hose that reuses the same buffers. Revisit reuseport BPF when there is a
multi-worker accept benchmark that is inbox-bound, and the process is
allowed to load BPF.
