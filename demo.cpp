#include <iostream>
#include <thread>
#include <chrono>
#include "gpendingpool.h"
#include "log.h"
using namespace std::chrono_literals;


int main()
{
    galois::gpendingpool pdp;
    pdp.start();
    while (true) {
        auto res = pdp.ready_queue_pop(5s);
        if (res) {
            galois::gpendingpool::socket_t socket;
            galois::gpendingpool::milliseconds wait_time;
            std::tie(socket, wait_time) = res.value();
            INFO("socket:[%d] waited for %u", socket, wait_time);
        } else {
            INFO("Time out...", "");
        }
    }
    pdp.stop();
    std::cout<<"exit"<<std::endl;
    return 0;
}