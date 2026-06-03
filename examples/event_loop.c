#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include <stdio.h>
#include <string.h>

#define EVENT_LOOP_PORT 54321

static int ticks = 0;
static suspenders_timer_t *tick_timer = NULL;

static void on_tick(void *arg) {
    (void)arg;
    printf("[event_loop] tick %d\n", ++ticks);
}

static void echo_client(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buf[256];
    ssize_t n;
    while ((n = suspenders_hose_read(client, buf, sizeof(buf))) > 0) {
        suspenders_hose_write(client, buf, (size_t)n);
    }
    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
}

static void server(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", EVENT_LOOP_PORT);
    if (!suspenders_hose_listen(&listener, uri)) {
        fprintf(stderr, "[event_loop] listen failed\n");
        return;
    }
    printf("[event_loop] echo server listening on %d\n", EVENT_LOOP_PORT);

    while (ticks < 8) {
        suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(*client));
        if (!client) continue;
        /* Deadline-bounded accept so the loop re-checks the tick count. */
        if (suspenders_hose_accept_dl(&listener, client,
                                      suspenders_now_ns() + 250 * 1000000ULL)) {
            suspenders_spawn(echo_client, client, SUSPENDERS_QOS_NORMAL);
        } else {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
        }
    }
    suspenders_hose_close(&listener);

    /* Stop the periodic timer so the scheduler can wind down. */
    if (tick_timer) {
        suspenders_timer_cancel(tick_timer);
        tick_timer = NULL;
    }
}

static void client(void *arg) {
    (void)arg;
    suspenders_hose_t conn;
    suspenders_hose_init(&conn, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", EVENT_LOOP_PORT);
    if (!suspenders_hose_dial(&conn, uri)) {
        fprintf(stderr, "[event_loop] dial failed\n");
        return;
    }

    const char *msg = "hello event loop";
    if (suspenders_hose_write(&conn, msg, strlen(msg)) > 0) {
        char buf[64] = {0};
        ssize_t n = suspenders_hose_read(&conn, buf, sizeof(buf) - 1);
        if (n > 0)
            printf("[event_loop] client received: %.*s\n", (int)n, buf);
    }
    suspenders_hose_close(&conn);
}

static void coordinator(void *arg) {
    (void)arg;
    /* A periodic timer keeps the loop alive and demonstrates timer integration. */
    tick_timer = suspenders_timer_create(250, true, on_tick, NULL);

    suspenders_spawn(server, NULL, SUSPENDERS_QOS_HIGH);

    /* Wait briefly for the server to start, then connect. */
    suspenders_sleep_ns(50000000); /* 50 ms */
    suspenders_spawn(client, NULL, SUSPENDERS_QOS_NORMAL);
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    suspenders_spawn(coordinator, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    printf("[event_loop] done\n");
    return 0;
}
