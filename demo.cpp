#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include "gpendingpool.h"
#include "log.h"
using namespace std::chrono_literals;

class my_pendingpool : public galois::gpendingpool {
public:
    unsigned int get_listen_port() const { return 8909;}
    unsigned int get_queue_len() const { return 5; }
    int get_alive_timeout_ms() const { return 4000; }
    int get_queuing_timeout_ms() const { return 4000; }
    int get_select_timeout_ms() const { return 1000; }
    size_t get_max_ready_queue_len() const { return 128; }
};

int main()
{
    my_pendingpool pdp;
    pdp.start();
    int cnt = 3;
    while (cnt--) {
        auto res = pdp.ready_queue_pop(3s);
        if (res) {
            galois::gpendingpool::socket_t socket;
            galois::gpendingpool::time_point_t connected_time;
            std::tie(socket, connected_time) = res.value();
            INFO("socket:[%d] poped", socket);
        } else {
            INFO("Time out...", "");
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    pdp.stop();
    INFO("exit", "");
    return 0;
}