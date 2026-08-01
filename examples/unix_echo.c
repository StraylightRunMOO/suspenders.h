/* unix_echo.c - Unix domain socket echo server using Suspenders coroutines
 *
 * Build:
 *   gcc -std=gnu11 -O2 -o unix_echo unix_echo.c \
 *       -I../include -I${MEMENTO_INCLUDE} $(pkg-config --cflags --libs liburing) -lpthread
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define SOCKET_PATH "/tmp/suspenders_unix_echo.sock"
#define NUM_CLIENTS 4
#define MESSAGES_PER_CLIENT 100

static volatile int running = 1;
static volatile int total_replies = 0;
static volatile int clients_done = 0;
static suspenders_cr_t *listener_cr = NULL;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    if (listener_cr) suspenders_cancel(listener_cr);
}

void unix_client(void *arg) {
    int id = (int)(intptr_t)arg;
    suspenders_hose_t conn;
    struct buf b = {0};
    suspenders_hose_init(&conn, &b);

    if (!suspenders_hose_dial(&conn, "unix://" SOCKET_PATH)) {
        fprintf(stderr, "Client %d: Failed to connect\n", id);
        return;
    }

    char send_buf[64];
    char recv_buf[64];
    int ok = 0;

    for (int i = 0; i < MESSAGES_PER_CLIENT && running; i++) {
        snprintf(send_buf, sizeof(send_buf), "Hello %d:%d", id, i);
        if (suspenders_hose_write(&conn, send_buf, strlen(send_buf)) <= 0) break;

        ssize_t n = suspenders_hose_read(&conn, recv_buf, sizeof(recv_buf) - 1);
        if (n <= 0) break;
        recv_buf[n] = '\0';

        if (strncmp(recv_buf, send_buf, n) == 0) {
            ok++;
            __atomic_fetch_add(&total_replies, 1, __ATOMIC_RELAXED);
        }
    }

    printf("Client %d: %d/%d echoed\n", id, ok, MESSAGES_PER_CLIENT);
    suspenders_hose_close(&conn);

    if (__atomic_add_fetch(&clients_done, 1, __ATOMIC_RELAXED) == NUM_CLIENTS) {
        running = 0;
        if (listener_cr) suspenders_cancel(listener_cr);
    }
}

void unix_handler(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buf[256];

    while (running) {
        ssize_t n = suspenders_hose_read(client, buf, sizeof(buf));
        if (n <= 0) break;
        if (suspenders_hose_write(client, buf, n) <= 0) break;
    }

    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
}

void unix_listener(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    /* Remove stale socket file */
    unlink(SOCKET_PATH);

    if (!suspenders_hose_listen(&listener, "unix://" SOCKET_PATH)) {
        fprintf(stderr, "[Server] Failed to listen\n");
        return;
    }

    printf("[Server] Listening on %s\n", SOCKET_PATH);

    while (running) {
        suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
        if (!client) continue;

        if (!suspenders_hose_accept(&listener, client)) {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
            if (!running) break;
            continue;
        }

        suspenders_spawn(unix_handler, client, SUSPENDERS_QOS_NORMAL);
    }

    suspenders_hose_close(&listener);
    unlink(SOCKET_PATH);
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Suspenders Unix Domain Socket Echo ===\n\n");

    suspenders_init(0, 256);
    listener_cr = suspenders_spawn(unix_listener, NULL, SUSPENDERS_QOS_HIGH);

    for (int i = 0; i < NUM_CLIENTS; i++)
        suspenders_spawn(unix_client, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);

    suspenders_run();
    suspenders_shutdown();

    printf("\n=== Results ===\n");
    printf("Total replies: %d (expected: %d)\n",
           total_replies, NUM_CLIENTS * MESSAGES_PER_CLIENT);

    return (total_replies == NUM_CLIENTS * MESSAGES_PER_CLIENT) ? 0 : 1;
}
