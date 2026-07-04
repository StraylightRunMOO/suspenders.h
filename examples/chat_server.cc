/* chat_server.cc - Chat server using the C++ API
 *
 * Demonstrates: Hose (RAII, move into lambda), Channel<T>, Mutex,
 * LockGuard, Queue (periodic stats), CleanupGuard, spawn with lambdas,
 * deadline I/O, QoS, try_send/try_recv.
 *
 * Test with: nc localhost 12351
 *   First line = room name, then chat.
 */

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <vector>
#include <array>
#include <algorithm>

static constexpr int PORT = 12351;
static constexpr int MAX_ROOMS = 4;

struct ChatMsg {
    char text[256];
    int  sender_id;
};

struct Room {
    char name[32];
    bool active = false;
    suspenders::Channel<ChatMsg>* broadcast = nullptr;
    std::vector<suspenders::Channel<ChatMsg>*> members;
    suspenders::Mutex lock;
};

static std::array<Room, MAX_ROOMS> rooms;
static suspenders::Mutex rooms_lock;
static std::atomic<int> total_msgs{0};
static std::atomic<int> total_conns{0};
static std::atomic<int> active_conns{0};

static Room* find_or_create_room(const char* name) {
    suspenders::LockGuard guard(rooms_lock);

    for (auto& r : rooms)
        if (r.active && std::strcmp(r.name, name) == 0)
            return &r;

    for (auto& r : rooms) {
        if (!r.active) {
            std::snprintf(r.name, sizeof(r.name), "%s", name);
            r.broadcast = new suspenders::Channel<ChatMsg>(32);
            r.members.clear();
            r.active = true;
            std::printf("[room] created '%s'\n", name);

            /* Start broadcaster */
            suspenders::spawn([&r] {
                ChatMsg msg;
                while (r.broadcast->recv(msg)) {
                    suspenders::LockGuard lg(r.lock);
                    for (auto* ch : r.members)
                        (void)ch->try_send(msg);
                }
            }, suspenders::QoS::High);

            return &r;
        }
    }
    return nullptr;
}

static void handle_client(suspenders::Hose client) {
    int id = total_conns.fetch_add(1);
    active_conns.fetch_add(1);

    char buf[512];

    /* Ask for room name with a 10-second deadline */
    (void)client.write("Room? ");
    ssize_t n = client.read_dl(buf, sizeof(buf) - 1,
                    suspenders::now_ns() + 10'000'000'000ULL);
    if (n <= 0) {
        std::printf("[%d] timed out\n", id);
        active_conns.fetch_sub(1);
        return;
    }
    buf[n] = '\0';
    if (char* nl = std::strchr(buf, '\n')) *nl = '\0';
    if (char* cr = std::strchr(buf, '\r')) *cr = '\0';
    if (buf[0] == '\0') std::strcpy(buf, "lobby");

    Room* room = find_or_create_room(buf);
    if (!room) {
        (void)client.write("All rooms full.\n");
        active_conns.fetch_sub(1);
        return;
    }

    auto* my_ch = new suspenders::Channel<ChatMsg>(8);
    {
        suspenders::LockGuard lg(room->lock);
        room->members.push_back(my_ch);
    }

    /* CleanupGuard: leave room on disconnect or cancellation */
    suspenders::CleanupGuard leave([room, my_ch] {
        {
            suspenders::LockGuard lg(room->lock);
            auto& m = room->members;
            m.erase(std::remove(m.begin(), m.end(), my_ch), m.end());
        }
        delete my_ch;
    });

    char welcome[128];
    std::snprintf(welcome, sizeof(welcome), "Joined '%s'. Start chatting!\n",
                  room->name);
    (void)client.write(std::string_view(welcome));
    std::printf("[%d] joined '%s'\n", id, room->name);

    for (;;) {
        n = client.read_dl(buf, sizeof(buf) - 1,
                suspenders::now_ns() + 50'000'000ULL);

        if (n > 0) {
            buf[n] = '\0';
            ChatMsg msg = {};
            msg.sender_id = id;
            std::snprintf(msg.text, sizeof(msg.text), "[%d] %s", id, buf);
            (void)room->broadcast->try_send(msg);
            total_msgs.fetch_add(1);
        } else if (suspenders_errno != SUSPENDERS_TIMEDOUT) {
            break;
        }

        ChatMsg incoming;
        while (my_ch->try_recv(incoming) == SUSPENDERS_OK) {
            if (incoming.sender_id != id)
                (void)client.write(std::string_view(incoming.text));
        }
    }

    std::printf("[%d] disconnected\n", id);
    active_conns.fetch_sub(1);
}

int main() {
    std::printf("=== Chat Server (C++) ===\n\n");
    suspenders::Context ctx;

    /* Periodic stats on the global queue */
    suspenders::spawn([] {
        auto q = suspenders::Queue::global(suspenders::QoS::Low);
        for (;;) {
            q.async([] {
                std::printf("[stats] %d active, %d total, %d msgs\n",
                            active_conns.load(), total_conns.load(),
                            total_msgs.load());
            });
            suspenders::sleep_ms(5000);
        }
    }, suspenders::QoS::Low);

    /* Listener */
    suspenders::spawn([] {
        suspenders::Hose listener;
        std::string uri = "tcp://0.0.0.0:" + std::to_string(PORT);
        if (!listener.listen(uri)) {
            std::fprintf(stderr, "[server] listen failed\n");
            return;
        }
        std::printf("[server] listening on port %d\n", PORT);

        for (;;) {
            suspenders::Hose client;
            if (listener.accept(client)) {
                suspenders::spawn([c = std::move(client)]() mutable {
                    handle_client(std::move(c));
                });
            }
        }
    }, suspenders::QoS::High);

    ctx.run();
    return 0;
}
