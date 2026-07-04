/* chat_server.c - Multi-room chat server over TCP
 *
 * Demonstrates: hoses (TCP), channels, select, mutex, cleanup handlers,
 * cancellation, task queues, deadline I/O, QoS priorities.
 *
 * Each client connects, picks a room, then sends messages. A per-room
 * broadcaster coroutine fans out messages to all members via channels.
 * A stats coroutine on a queue publishes a periodic summary.
 *
 * Test with: nc localhost 12350
 *   Type a room name on the first line, then chat.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define PORT         12350
#define MAX_ROOMS    4
#define MAX_MEMBERS  16
#define MSG_LEN      256
#define BUF_SIZE     512

typedef struct {
    char text[MSG_LEN];
    int  sender_id;
} chat_msg_t;

typedef struct {
    char name[32];
    suspenders_chan_t *broadcast_ch;       /* messages TO the room */
    suspenders_chan_t *member_chs[MAX_MEMBERS];  /* per-member delivery */
    int  member_count;
    suspenders_mutex_t lock;
    bool active;
} room_t;

static room_t rooms[MAX_ROOMS];
static suspenders_mutex_t rooms_lock;
static _Atomic int total_messages = 0;
static _Atomic int total_connections = 0;
static _Atomic int active_connections = 0;
static volatile int running = 1;

static room_t *find_or_create_room(const char *name) {
    suspenders_mutex_lock(&rooms_lock);
    room_t *found = NULL;

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active && strcmp(rooms[i].name, name) == 0) {
            found = &rooms[i];
            break;
        }
    }

    if (!found) {
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (!rooms[i].active) {
                found = &rooms[i];
                snprintf(found->name, sizeof(found->name), "%s", name);
                found->broadcast_ch = suspenders_chan_create(sizeof(chat_msg_t), 32);
                found->member_count = 0;
                found->active = true;
                suspenders_mutex_init(&found->lock);
                printf("[room] created '%s'\n", name);
                break;
            }
        }
    }

    suspenders_mutex_unlock(&rooms_lock);
    return found;
}

static suspenders_chan_t *room_join(room_t *r) {
    suspenders_mutex_lock(&r->lock);
    suspenders_chan_t *ch = NULL;
    if (r->member_count < MAX_MEMBERS) {
        ch = suspenders_chan_create(sizeof(chat_msg_t), 8);
        r->member_chs[r->member_count++] = ch;
    }
    suspenders_mutex_unlock(&r->lock);
    return ch;
}

static void room_leave(room_t *r, suspenders_chan_t *ch) {
    suspenders_mutex_lock(&r->lock);
    for (int i = 0; i < r->member_count; i++) {
        if (r->member_chs[i] == ch) {
            r->member_chs[i] = r->member_chs[--r->member_count];
            break;
        }
    }
    suspenders_mutex_unlock(&r->lock);
    suspenders_chan_close(ch);
    suspenders_chan_destroy(ch);
}

/* Broadcaster: reads from room's broadcast channel, fans out to members */
static void broadcaster(void *arg) {
    room_t *r = (room_t *)arg;
    suspenders_setname("broadcaster");

    for (;;) {
        chat_msg_t msg;
        int rc = suspenders_chan_recv(r->broadcast_ch, &msg);
        if (rc != SUSPENDERS_OK) break;

        suspenders_mutex_lock(&r->lock);
        for (int i = 0; i < r->member_count; i++) {
            /* Skip the sender's own channel */
            suspenders_chan_try_send(r->member_chs[i], &msg);
        }
        suspenders_mutex_unlock(&r->lock);
    }
}

/* Cleanup: remove member from room on disconnect/cancel */
typedef struct {
    room_t *room;
    suspenders_chan_t *ch;
} leave_ctx_t;

static void cleanup_leave(void *arg) {
    leave_ctx_t *lc = (leave_ctx_t *)arg;
    room_leave(lc->room, lc->ch);
}

/* Per-client handler */
static void client_handler(void *arg) {
    suspenders_hose_t *hose = (suspenders_hose_t *)arg;
    int id = atomic_fetch_add(&total_connections, 1);
    atomic_fetch_add(&active_connections, 1);

    char buf[BUF_SIZE];

    /* Prompt for room name with a 10-second deadline */
    const char *prompt = "Room name? ";
    suspenders_hose_write(hose, prompt, strlen(prompt));

    ssize_t n = suspenders_hose_read_dl(hose, buf, sizeof(buf) - 1,
                    suspenders_now_ns() + 10ULL * 1000000000);
    if (n <= 0) {
        printf("[client %d] timed out waiting for room name\n", id);
        goto done;
    }

    /* Strip newline */
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    nl = strchr(buf, '\r');
    if (nl) *nl = '\0';
    if (buf[0] == '\0') {
        snprintf(buf, sizeof(buf), "lobby");
    }

    room_t *room = find_or_create_room(buf);
    if (!room) {
        const char *full = "All rooms full.\n";
        suspenders_hose_write(hose, full, strlen(full));
        goto done;
    }

    suspenders_chan_t *my_ch = room_join(room);
    if (!my_ch) {
        const char *full = "Room full.\n";
        suspenders_hose_write(hose, full, strlen(full));
        goto done;
    }

    /* Ensure we leave the room even if canceled */
    leave_ctx_t lc = { room, my_ch };
    suspenders_cleanup_t cleanup;
    suspenders_cleanup_push(&cleanup, cleanup_leave, &lc);

    /* Start the room's broadcaster if this is the first member */
    if (room->member_count == 1) {
        suspenders_spawn(broadcaster, room, SUSPENDERS_QOS_HIGH);
    }

    char welcome[128];
    snprintf(welcome, sizeof(welcome), "Joined '%s' (%d online). Start chatting!\n",
             room->name, room->member_count);
    suspenders_hose_write(hose, welcome, strlen(welcome));

    printf("[client %d] joined room '%s'\n", id, room->name);

    /* Main loop: select between incoming network data and room messages */
    for (;;) {
        /* Read from network with a short deadline so we can also check
         * the room channel. A real chat server would use select over
         * both, but we alternate: read network (50ms deadline), then
         * drain room messages. */
        n = suspenders_hose_read_dl(hose, buf, sizeof(buf) - 1,
                suspenders_now_ns() + 50ULL * 1000000);

        if (n > 0) {
            buf[n] = '\0';
            chat_msg_t msg = { .sender_id = id };
            snprintf(msg.text, MSG_LEN, "[%d] %s", id, buf);
            suspenders_chan_try_send(room->broadcast_ch, &msg);
            atomic_fetch_add(&total_messages, 1);
        } else if (suspenders_errno != SUSPENDERS_TIMEDOUT) {
            break;  /* real error or EOF */
        }

        /* Drain incoming room messages to the client */
        chat_msg_t incoming;
        while (suspenders_chan_try_recv(my_ch, &incoming) == SUSPENDERS_OK) {
            if (incoming.sender_id != id) {
                suspenders_hose_write(hose, incoming.text, strlen(incoming.text));
            }
        }
    }

    suspenders_cleanup_pop(1);  /* execute: leave the room */

done:
    printf("[client %d] disconnected\n", id);
    suspenders_hose_close(hose);
    memento_thread_heap_free(memento_thread_heap_get(), hose,
                             sizeof(suspenders_hose_t));
    atomic_fetch_sub(&active_connections, 1);
}

/* Periodic stats on a task queue */
static void print_stats(void *arg) {
    (void)arg;
    printf("[stats] connections: %d active, %d total, %d messages\n",
           atomic_load(&active_connections),
           atomic_load(&total_connections),
           atomic_load(&total_messages));
}

static void stats_scheduler(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_get_global_queue(SUSPENDERS_QOS_LOW);

    while (running) {
        suspenders_queue_async(q, print_stats, NULL);
        int rc = suspenders_sleep_ns(5ULL * 1000000000);
        if (rc != SUSPENDERS_OK) break;
    }
}

static void listener(void *arg) {
    (void)arg;
    suspenders_hose_t srv;
    suspenders_hose_init(&srv, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://0.0.0.0:%d", PORT);
    if (!suspenders_hose_listen(&srv, uri)) {
        fprintf(stderr, "[server] listen failed on %s\n", uri);
        return;
    }
    printf("[server] chat server on port %d\n", PORT);
    printf("[server] connect with: nc localhost %d\n\n", PORT);

    while (running) {
        suspenders_hose_t *client = memento_thread_heap_alloc(
            memento_thread_heap_get(), sizeof(suspenders_hose_t));
        if (!client) continue;

        if (!suspenders_hose_accept(&srv, client)) {
            memento_thread_heap_free(memento_thread_heap_get(),
                                     client, sizeof(*client));
            continue;
        }
        suspenders_go(client_handler, client);
    }

    suspenders_hose_close(&srv);
}

int main(void) {
    printf("=== Chat Server ===\n\n");

    suspenders_init(4, 256);
    suspenders_mutex_init(&rooms_lock);

    suspenders_spawn(listener, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(stats_scheduler, NULL, SUSPENDERS_QOS_LOW);

    suspenders_run();
    suspenders_shutdown();
    return 0;
}
