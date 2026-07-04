/* select_mux.c - Channel multiplexer with select and deadlines
 *
 * Demonstrates: suspenders_select, suspenders_select_dl, buffered channels,
 * try_send/try_recv, channel close semantics, deadline handling.
 *
 * A "ticker" coroutine sends timestamps to a tick channel every 50ms.
 * A "commands" coroutine sends string commands on a separate channel.
 * A "mux" coroutine uses select to handle whichever arrives first,
 * with a 200ms deadline so it can print a heartbeat when idle.
 * A "drain" coroutine demonstrates try_recv to non-blocking poll a channel.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

static suspenders_chan_t *tick_ch = NULL;   /* uint64_t timestamps */
static suspenders_chan_t *cmd_ch  = NULL;   /* int command codes */
static suspenders_chan_t *log_ch  = NULL;   /* int log entries (buffered) */

enum { CMD_GREET = 1, CMD_STATUS = 2, CMD_QUIT = 3 };

static _Atomic int ticks_handled  = 0;
static _Atomic int cmds_handled   = 0;
static _Atomic int logs_drained   = 0;
static _Atomic int heartbeats     = 0;

static void ticker(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        uint64_t ts = suspenders_now_ns();
        int rc = suspenders_chan_send(tick_ch, &ts);
        if (rc != SUSPENDERS_OK) break;
        suspenders_sleep_ns(50ULL * 1000000);
    }
}

static void commands(void *arg) {
    (void)arg;
    int cmds[] = { CMD_GREET, CMD_STATUS, CMD_GREET, CMD_STATUS, CMD_QUIT };

    for (int i = 0; i < 5; i++) {
        suspenders_sleep_ns(150ULL * 1000000);
        int rc = suspenders_chan_send(cmd_ch, &cmds[i]);
        if (rc != SUSPENDERS_OK) break;
    }
}

static void mux(void *arg) {
    (void)arg;

    for (;;) {
        uint64_t ts;
        int cmd;
        suspenders_chan_op_t ops[] = {
            { tick_ch, &ts,  false },
            { cmd_ch,  &cmd, false },
        };

        int idx = suspenders_select_dl(ops, 2,
                      suspenders_now_ns() + 200ULL * 1000000);

        if (idx == SUSPENDERS_TIMEDOUT) {
            atomic_fetch_add(&heartbeats, 1);
            printf("[mux] heartbeat (idle 200ms)\n");
            continue;
        }

        if (idx < 0) break;  /* canceled or error */

        if (suspenders_errno == SUSPENDERS_CLOSED) {
            printf("[mux] channel %d closed\n", idx);
            if (idx == 1) break;  /* cmd channel closed = done */
            continue;
        }

        if (idx == 0) {
            atomic_fetch_add(&ticks_handled, 1);
            /* Silently count ticks; log every 5th */
            if (atomic_load(&ticks_handled) % 5 == 0)
                printf("[mux] tick #%d at %llu ns\n",
                       atomic_load(&ticks_handled),
                       (unsigned long long)ts);
        } else {
            atomic_fetch_add(&cmds_handled, 1);
            switch (cmd) {
            case CMD_GREET:
                printf("[mux] command: GREET\n");
                break;
            case CMD_STATUS:
                printf("[mux] command: STATUS (ticks=%d cmds=%d)\n",
                       atomic_load(&ticks_handled),
                       atomic_load(&cmds_handled));
                break;
            case CMD_QUIT:
                printf("[mux] command: QUIT\n");
                /* Log a summary to the log channel */
                {
                    int summary = atomic_load(&ticks_handled);
                    suspenders_chan_try_send(log_ch, &summary);
                }
                suspenders_chan_close(tick_ch);
                suspenders_chan_close(cmd_ch);
                suspenders_chan_close(log_ch);
                return;
            }

            /* Non-blocking: try to log the command to the log channel */
            int entry = cmd;
            int rc = suspenders_chan_try_send(log_ch, &entry);
            if (rc == SUSPENDERS_FULL)
                printf("[mux] log channel full, dropping\n");
        }
    }
}

static void drain_logs(void *arg) {
    (void)arg;
    /* Wait a bit, then non-blocking drain whatever accumulated. */
    suspenders_sleep_ns(800ULL * 1000000);

    int entry;
    while (suspenders_chan_try_recv(log_ch, &entry) == SUSPENDERS_OK) {
        atomic_fetch_add(&logs_drained, 1);
    }
    printf("[drain] drained %d log entries\n", atomic_load(&logs_drained));
}

int main(void) {
    printf("=== Select Multiplexer ===\n\n");

    suspenders_init(0, 256);

    tick_ch = suspenders_chan_create(sizeof(uint64_t), 0);  /* rendezvous */
    cmd_ch  = suspenders_chan_create(sizeof(int), 0);       /* rendezvous */
    log_ch  = suspenders_chan_create(sizeof(int), 8);       /* buffered */

    suspenders_go(ticker, NULL);
    suspenders_go(commands, NULL);
    suspenders_go(drain_logs, NULL);
    suspenders_spawn(mux, NULL, SUSPENDERS_QOS_HIGH);

    suspenders_run();

    printf("\n=== Results ===\n");
    printf("Ticks: %d  Commands: %d  Heartbeats: %d  Logs: %d\n",
           atomic_load(&ticks_handled), atomic_load(&cmds_handled),
           atomic_load(&heartbeats), atomic_load(&logs_drained));

    suspenders_chan_destroy(tick_ch);
    suspenders_chan_destroy(cmd_ch);
    suspenders_chan_destroy(log_ch);
    suspenders_shutdown();

    return (atomic_load(&cmds_handled) == 5) ? 0 : 1;
}
