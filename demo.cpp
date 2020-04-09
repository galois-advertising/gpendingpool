#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
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
            galois::gpendingpool::time_point_t connected_time;
            std::tie(socket, connected_time) = res.value();
            INFO("socket:[%d] poped", socket);
        } else {
            //INFO("Time out...", "");
        }
    }
    pdp.stop();
    std::cout<<"exit"<<std::endl;
    return 0;
}