/* tcp_echo.c - Simple TCP echo server using Suspenders coroutines
 * 
 * Build:
 *   gcc -std=gnu11 -O2 -o tcp_echo tcp_echo.c \
 *       -I../include -I${MEMENTO_INCLUDE} $(pkg-config --cflags --libs liburing) -lpthread
 * 
 * Run:
 *   ./tcp_echo
 * 
 * Test with:
 *   echo "Hello" | nc localhost 12345
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

#define SERVER_PORT 12345
#define BUFFER_SIZE 4096

static volatile int running = 1;
static volatile int active_connections = 0;
static volatile int total_connections = 0;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Echo handler - one per client connection */
void echo_handler(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buffer[BUFFER_SIZE];
    
    int conn_num = __atomic_fetch_add(&total_connections, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&active_connections, 1, __ATOMIC_RELAXED);
    
    printf("[Conn %d fd=%d] Connected (active: %d)\n", 
           conn_num, (int)client->fd, active_connections);
    
    while (running) {
        /* Async read via backend */
        ssize_t bytes_read = suspenders_hose_read(client, buffer, sizeof(buffer) - 1);
        
        if (bytes_read <= 0) {
            break;
        }
        
        buffer[bytes_read] = '\0';
        printf("[Conn %d] Received %zd bytes: %s", conn_num, bytes_read, 
               buffer[bytes_read-1] == '\n' ? buffer : strcat(buffer, "\n"));
        
        /* Echo back */
        if (suspenders_hose_write(client, buffer, bytes_read) <= 0) {
            break;
        }
    }
    
    printf("[Conn %d] Disconnected\n", conn_num);
    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
    
    __atomic_fetch_sub(&active_connections, 1, __ATOMIC_RELAXED);
}

/* Server listener */
void server_listener(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    struct buf b = {0};

    suspenders_hose_init(&listener, &b);
    
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://0.0.0.0:%d", SERVER_PORT);
    
    if (!suspenders_hose_listen(&listener, uri)) {
        fprintf(stderr, "[Server] Failed to listen on %s\n", uri);
        return;
    }
    
    printf("[Server] Listening on port %d\n", SERVER_PORT);
    printf("[Server] Test with: echo 'Hello' | nc -q 1 localhost %d\n\n", SERVER_PORT);
    
    while (running) {
        suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
        if (!client) continue;

        if (!suspenders_hose_accept(&listener, client)) {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
            continue;
        }
        
        /* Spawn handler for this client */
        suspenders_spawn(echo_handler, client, SUSPENDERS_QOS_NORMAL);
    }
    
    printf("[Server] Shutting down\n");
    suspenders_hose_close(&listener);
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("=== Suspenders TCP Echo Server ===\n\n");
    
    suspenders_init(4, 256);
    suspenders_spawn(server_listener, NULL, SUSPENDERS_QOS_HIGH);
    
    /* Run scheduler */
    suspenders_run();
    
    printf("\n=== Results ===\n");
    printf("Total connections: %d\n", total_connections);
    printf("Active at shutdown: %d\n", active_connections);
    
    suspenders_shutdown();
    
    return 0;
}
