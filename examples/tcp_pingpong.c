/* tcp_pingpong.c - TCP PING/PONG example using Suspenders coroutines
 * 
 * This example demonstrates:
 * - TCP server listening on a port
 * - TCP client connecting and sending messages
 * - Coroutine-per-connection model
 * - Async I/O with automatic coroutine suspension
 * 
 * Build:
 *   gcc -std=gnu11 -O2 -o tcp_pingpong tcp_pingpong.c \
 *       -I../include -I../third_party $(pkg-config --cflags --libs liburing) -lpthread
 * 
 * Run:
 *   ./tcp_pingpong
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define SERVER_PORT 12345
#define NUM_CLIENTS 4
#define NUM_PINGS 1000

static volatile int running = 1;
static volatile int total_pongs = 0;
static volatile int clients_done = 0;
static suspenders_cr_t *server_cr = NULL;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Server handler - echoes PONG for each PING received */
void server_handler(void *arg) {
    hose_t *client = (hose_t*)arg;
    char read_buf[1024];

    while (running) {
        ssize_t n = hose_read(client, read_buf, sizeof(read_buf));
        if (n <= 0) break;

        /* Append to the hose's internal buffer for stream reassembly */
        buf_append(client->buffer, read_buf, n);

        /* Scan for complete PING messages and reply */
        while (client->buffer->data && strstr(client->buffer->data, "PING")) {
            if (hose_write(client, "PONG\n", 5) <= 0) goto done;
            /* Find end of this message line */
            char *nl = strchr(client->buffer->data, '\n');
            if (nl) {
                size_t consumed = (size_t)(nl - client->buffer->data) + 1;
                size_t remaining = client->buffer->len - consumed;
                memmove(client->buffer->data, nl + 1, remaining);
                client->buffer->len = remaining;
                client->buffer->data[remaining] = '\0';
            } else {
                client->buffer->len = 0;
                client->buffer->data[0] = '\0';
            }
        }
    }
done:
    hose_close(client);
    if (client->buffer) {
        if (client->buffer->data)
            BUF_FREE(client->buffer->data, client->buffer->cap + 1);
        memento_thread_heap_free(memento_thread_heap_get(), client->buffer, sizeof(struct buf));
    }
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(hose_t));
}

void server_listener(void *arg) {
    (void)arg;
    hose_t listener;
    hose_init(&listener, suspenders_ring(), NULL);
    
    if (!hose_listen(&listener, "tcp://0.0.0.0:12345")) return;

    while (running) {
        // Allocate via the memento-backed system
        hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(hose_t));
        
        if (!hose_accept(&listener, client)) {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(hose_t));
            if (!running) break;
            continue;
        }
        
        client->buffer = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(struct buf));
        memset(client->buffer, 0, sizeof(struct buf));

        suspenders_spawn(server_handler, client, SUSPENDERS_QOS_NORMAL);
    }
}

/* Client coroutine - sends PINGs and expects PONGs */
void client_worker(void *arg) {
    int client_id = (int)(intptr_t)arg;
    hose_t conn;
    struct buf b = {0};
    char recv_buf[256];
    int pongs_received = 0;

    hose_init(&conn, suspenders_ring(), &b);
    
    /* Connect to server */
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", SERVER_PORT);
    
    if (!hose_dial(&conn, uri)) {
        fprintf(stderr, "Client %d: Failed to connect to %s\n", client_id, uri);
        return;
    }
    
    printf("Client %d: Connected to %s\n", client_id, uri);
    
    /* Send PINGs and wait for PONGs */
    for (int i = 0; i < NUM_PINGS && running; i++) {
        /* Send PING */
        char ping_msg[64];
        snprintf(ping_msg, sizeof(ping_msg), "PING %d from client %d\n", i, client_id);
        
        if (hose_write(&conn, ping_msg, strlen(ping_msg)) <= 0) {
            fprintf(stderr, "Client %d: Write failed\n", client_id);
            break;
        }

        /* Read PONG - auto-suspends until response arrives */
        memset(recv_buf, 0, sizeof(recv_buf));
        ssize_t n = hose_read(&conn, recv_buf, sizeof(recv_buf) - 1);
        if (n <= 0) {
            fprintf(stderr, "Client %d: Read failed\n", client_id);
            break;
        }
        
        if (strncmp(recv_buf, "PONG", 4) == 0) {
            pongs_received++;
            __atomic_fetch_add(&total_pongs, 1, __ATOMIC_RELAXED);
        }
    }
    
    printf("Client %d: Received %d/%d PONGs\n", client_id, pongs_received, NUM_PINGS);

    hose_close(&conn);

    /* Signal shutdown when all clients are done */
    if (__atomic_add_fetch(&clients_done, 1, __ATOMIC_RELAXED) == NUM_CLIENTS) {
        running = 0;
        /* Cancel the server's pending accept operation */
        struct io_uring_sqe *sqe = io_uring_get_sqe(suspenders_ring());
        if (sqe) {
            io_uring_prep_cancel(sqe, server_cr, 0);
            io_uring_sqe_set_data(sqe, NULL);
            io_uring_submit(suspenders_ring());
        }
    }
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== Suspenders TCP PING/PONG Example ===\n\n");
    printf("Starting %d clients, each sending %d PINGs...\n\n", NUM_CLIENTS, NUM_PINGS);
    
    /* Initialize Suspenders */
    suspenders_init(4, 256);  /* 4 workers, 256 io_uring entries */
    
    /* Start server listener */
    server_cr = suspenders_spawn(server_listener, NULL, SUSPENDERS_QOS_HIGH);

    /* Start clients */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        suspenders_spawn(client_worker, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);
    }
    
    /* Run the scheduler */
    suspenders_run();
    
    printf("\n=== Results ===\n");
    printf("Total PONGs received: %d (expected: %d)\n", 
           total_pongs, NUM_CLIENTS * NUM_PINGS);
    
    suspenders_shutdown();
    
    return (total_pongs == NUM_CLIENTS * NUM_PINGS) ? 0 : 1;
}
