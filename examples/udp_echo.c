/* udp_echo.c - UDP echo client/server using Suspenders coroutines
 *
 * A UDP server binds to a port and echoes back any datagrams it receives.
 * A built-in client sends messages and verifies the echoed replies.
 *
 * Build:
 *   gcc -std=gnu11 -O2 -o udp_echo udp_echo.c \
 *       -I../include -I../third_party $(pkg-config --cflags --libs liburing) -lpthread
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define SERVER_PORT  12346
#define NUM_MESSAGES 1000

static volatile int running = 1;
static volatile int server_done = 0;
static volatile int client_done = 0;
static volatile int replies_ok = 0;

void udp_server(void *arg) {
    (void)arg;
    suspenders_hose_t server;
    suspenders_hose_init(&server, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "udp://0.0.0.0:%d", SERVER_PORT);
    if (!suspenders_hose_listen(&server, uri)) {
        fprintf(stderr, "[Server] Failed to bind %s\n", uri);
        return;
    }

    printf("[Server] Listening on UDP port %d\n", SERVER_PORT);

    char buf[1024];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (running && replies_ok < NUM_MESSAGES) {
        ssize_t n = suspenders_hose_recvfrom(&server, buf, sizeof(buf) - 1,
                                  (struct sockaddr*)&client_addr, &addr_len);
        if (n < 0) {
            if (!running) break;
            continue;
        }

        /* Echo the datagram back to the sender */
        ssize_t sent = suspenders_hose_sendto(&server, buf, n,
                                   (struct sockaddr*)&client_addr, addr_len);
        if (sent < 0) break;
    }

    printf("[Server] Shutting down\n");
    suspenders_hose_close(&server);
    server_done = 1;
}

void udp_client(void *arg) {
    (void)arg;
    suspenders_hose_t client;
    struct buf b = {0};
    suspenders_hose_init(&client, &b);

    char uri[64];
    snprintf(uri, sizeof(uri), "udp://127.0.0.1:%d", SERVER_PORT);
    if (!suspenders_hose_dial(&client, uri)) {
        fprintf(stderr, "[Client] Failed to dial %s\n", uri);
        return;
    }

    printf("[Client] Sending %d messages...\n", NUM_MESSAGES);

    char send_buf[64];
    char recv_buf[64];

    for (int i = 0; i < NUM_MESSAGES && running; i++) {
        snprintf(send_buf, sizeof(send_buf), "MSG %d", i);
        if (suspenders_hose_write(&client, send_buf, strlen(send_buf)) <= 0) {
            fprintf(stderr, "[Client] Send failed at %d\n", i);
            break;
        }

        ssize_t n = suspenders_hose_read(&client, recv_buf, sizeof(recv_buf) - 1);
        if (n <= 0) {
            fprintf(stderr, "[Client] Recv failed at %d\n", i);
            break;
        }
        recv_buf[n] = '\0';

        if (strncmp(recv_buf, send_buf, n) == 0)
            __atomic_fetch_add(&replies_ok, 1, __ATOMIC_RELAXED);
    }

    printf("[Client] Received %d/%d valid replies\n", replies_ok, NUM_MESSAGES);
    suspenders_hose_close(&client);
    client_done = 1;
    running = 0;
}

int main(void) {
    printf("=== Suspenders UDP Echo ===\n\n");

    suspenders_init(0, 256);
    suspenders_spawn(udp_server, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(udp_client, NULL, SUSPENDERS_QOS_NORMAL);

    suspenders_run();
    suspenders_shutdown();

    printf("\n=== Results ===\n");
    printf("Valid replies: %d/%d\n", replies_ok, NUM_MESSAGES);
    printf("%s\n", (replies_ok == NUM_MESSAGES) ? "SUCCESS" : "FAILURE");

    return (replies_ok == NUM_MESSAGES) ? 0 : 1;
}
