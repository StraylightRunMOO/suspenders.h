# SUSPENDERS_FIX.md — upstream suspenders.h io_uring defect

Status: **fixed in v1.1.0**
Upstream: [StraylightRunMOO/suspenders.h](https://github.com/StraylightRunMOO/suspenders.h)

## Fix summary (v1.1.0)

1. **Eager submit** — `io_uring_submit` after prep of each blocking op batch
   (`s_iou_op`) and after wake-fd poll arm (`s_iou_arm_wake`).
2. **Kernel gate** — io_uring is selected only when the running kernel is
   ≥ 5.19 (COOP_TASKRUN era). Older kernels fall back to the poll backend
   at `suspenders_init` time (same machine, no rebuild required).
3. **Atomic op completion** — `suspenders_io_op_t.completed` / `result` use
   RELEASE/ACQUIRE stores and loads.
4. **Regression** — `test_loop_smoke` mixes listener accept, per-conn hose
   I/O, timers, and channels.

On kernels ≥ 5.19, re-enable full io_uring confidence by running
`./build/suspenders-tests --filter=loop_smoke` and
`ctest --repeat until-fail:20`. On 5.15-class hosts, default builds use
poll and should stay green without `-DSUSPENDERS_FORCE_POLL`.

---

## Original report (historical)

Status at report time: open on v1.0.0 · Local mitigation was poll backend

## Summary

The suspenders.h **io_uring backend deadlocks** on this platform
(aarch64, Linux 5.15.148-tegra, liburing present, `kernel.io_uring_disabled=0`).
The scheduler parks all workers in `io_uring_enter(GETEVENTS)` while
completed/pending network events exist, and coroutines blocked in hose I/O
never resume. The **poll backend (`SUSPENDERS_FORCE_POLL`) is fully
functional** on the same machine and passes the whole test suite.

## Platform

| Item | Value |
| --- | --- |
| Arch | aarch64 (NVIDIA Tegra / Jetson, out-of-tree kernel) |
| Kernel | 5.15.148-tegra |
| liburing | system (`/usr/lib/aarch64-linux-gnu/liburing.so`) |
| Compiler | Clang (also reproduces logic under GCC) |
| suspenders | 1.0.0, io_uring default backend (no `SQPOLL`) |

## Reproduction

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo          # io_uring ON (default)
cmake --build build -j
./build/loop_smoke     # hangs: listener binds, then silence
```

Observed: **9/9 consecutive runs hang**. With
`-DSMAUG_SUSPENDERS_IOURING=OFF`: **5/5 pass**, and `ctest` is green.

Timeline of a hanging run:

1. `sm_loop: listening on tcp://0.0.0.0:14043` — bind/listen succeeded.
2. The smoke client connects (TCP handshake completes; connection sits in
   the listener backlog), sends `look`/`north`/`quit`, then blocks in
   `read()` waiting for `ACK: quit` forever.
3. No greeting/ACK is ever processed; `sm_loop_stop` never fires.

Notably: connecting a *second*, external client (`telnet 127.0.0.1 14043`)
while hung **unblocks the whole scheduler** — pending completions get
flushed, the built-in client receives all ACKs, and the test then passes.
This points at a lost/missed wakeup in the io_uring wait path, not a
logic error in sm_loop.

## Evidence collected

- Hung process thread states (`/proc/<pid>/task/*/wchan`):
  - both suspenders workers: `__arm64_sys_io_uring_enter`
    (parked inside `s_backend_wait` → `io_uring_wait_cqe[_timeout]`)
  - main thread (smoke client): `futex_wait_queue_me`
- `io_uring_queue_init` succeeds; plain one-shot io_uring ops work.
- Isolated suspenders/io_uring repros on the same machine **pass**:
  - coroutine + `suspenders_hose_listen`/`accept`/`hose_read` echo — OK
  - coroutine + repeated `suspenders_sleep_ns` (timer heap path) — OK
- The deadlock needs the full sm_loop mix (listener + per-conn recv +
  pulse timers + channel wakeups) — i.e. it is an interaction/timing
  defect in the backend, not a broken syscall.
- Poll backend (`SUSPENDERS_FORCE_POLL`) passes `loop_smoke` 5/5 and all
  unit suites, so sm_loop's use of the suspenders API is sound (including
  the TLS init/run/shutdown contract).

## Suspected root cause (upstream audit targets)

All line numbers refer to `suspenders.h` @ `7b10395`:

1. **Missed CQE wakeup on 5.15** — `s_backend_wait` (L3003) submits, flushes,
   then sleeps in `io_uring_wait_cqe`. On 5.15 (pre-`IORING_SETUP_COOP_TASKRUN`,
   pre-single-issue-wakeup rework) completions are delivered via task_work;
   combined with one-shot `poll_add` accept arming this kernel had several
   known lost-wakeup/poll-arm races (fixed upstream in the 5.16–5.19
   window). Symptom match: CQE pending, waiter not woken until *another*
   SQE submission pokes the ring (the external-telnet unblocking effect).
2. **Submission only on idle** — `s_iou_op` (L2902) preps SQEs but
   `io_uring_submit` is called only from `s_backend_wait` (idle path) or on
   SQ-full (`s_iou_get_sqe`, L2872). A worker that stays busy never submits;
   an op whose completion everyone else waits for can sit unsubmitted in
   the SQ ring. Latency bug at best; contributes to deadlock windows.
3. **aarch64 visibility** — `suspenders_io_op_t.completed/result`
   (L2816/L2928) are plain non-atomic fields. Today submission, CQE flush
   (`s_iou_flush_cqes`, L2975) and resumption are confined to one worker
   (per-worker ring + TLS ready queues), so this is currently safe — but it
   is one scheduler change away from a TSO-hidden race. Worth hardening
   (release store on completion, acquire on resume) while fixing #1/#2.

## Required upstream fix

1. Reproduce on kernel ≤ 5.15 (any aarch64 or x86_64 box with a 5.15 LTS
   kernel) with the `loop_smoke`-equivalent: listener accept loop +
   per-connection recv + periodic timer + channel producer/consumer.
2. Fix the wait path so a posted CQE reliably wakes the parked worker on
   pre-`COOP_TASKRUN` kernels — or gate the io_uring backend on a kernel
   version that provides reliable wakeup semantics (≥ 5.19 suggested;
   verify empirically) and fall back to the poll backend otherwise, the
   same way liburing absence already falls back.
3. Submit eagerly: `io_uring_submit` (or `io_uring_submit_and_wait` where
   appropriate) right after prepping a blocking op in `s_iou_op`, instead
   of deferring to the idle path.
4. Add CI coverage: aarch64 + an old-LTS-kernel job running the network
   smoke tests, so TSO-hidden and kernel-version-specific regressions are
   caught.

## Local mitigation (in effect)

- Build with `-DSMAUG_SUSPENDERS_IOURING=OFF` →
  `SUSPENDERS_FORCE_POLL` is defined on `dep_suspenders`
  (CMakeLists.txt). The current `build/` tree is configured this way and
  the full suite passes.
- No code changes were needed in `src/modern/`; sm_loop is
  backend-agnostic by design (see `docs/INTEGRATION.md`).

Once upstream ships a fix, re-enable io_uring, run `loop_smoke` 10× and
`ctest --repeat until-fail:20`, and delete this file's mitigation note.
