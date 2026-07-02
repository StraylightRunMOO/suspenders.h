#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include <stdio.h>
#include <string.h>

#define EVENT_LOOP_PORT 54321

static int ticks = 0;

static void on_tick(void *arg) {
    (void)arg;
    printf("[event_loop] tick %d\n", ++ticks);
}

static void echo_client(void *arg) {
    hose_t *client = (hose_t*)arg;
    char buf[256];
    ssize_t n;
    while ((n = hose_read(client, buf, sizeof(buf))) > 0) {
        hose_write(client, buf, (size_t)n);
    }
    hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
}

static void server(void *arg) {
    (void)arg;
    hose_t listener;
    hose_init(&listener, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", EVENT_LOOP_PORT);
    if (!hose_listen(&listener, uri)) {
        fprintf(stderr, "[event_loop] listen failed\n");
        return;
    }
    printf("[event_loop] echo server listening on %d\n", EVENT_LOOP_PORT);

    while (ticks < 8) {
        hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(*client));
        if (!client) continue;
        if (hose_accept(&listener, client)) {
            suspenders_spawn(echo_client, client, SUSPENDERS_QOS_NORMAL);
        } else {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
        }
    }
    hose_close(&listener);
}

static void client(void *arg) {
    (void)arg;
    hose_t conn;
    hose_init(&conn, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", EVENT_LOOP_PORT);
    if (!hose_dial(&conn, uri)) {
        fprintf(stderr, "[event_loop] dial failed\n");
        return;
    }

    const char *msg = "hello event loop";
    if (hose_write(&conn, msg, strlen(msg)) > 0) {
        char buf[64] = {0};
        ssize_t n = hose_read(&conn, buf, sizeof(buf) - 1);
        if (n > 0)
            printf("[event_loop] client received: %.*s\n", (int)n, buf);
    }
    hose_close(&conn);
}

static void coordinator(void *arg) {
    (void)arg;
    /* A periodic timer keeps the loop alive and demonstrates timer integration. */
    suspenders_timer_create(250, true, on_tick, NULL);

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
