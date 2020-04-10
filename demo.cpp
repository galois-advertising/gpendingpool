#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include "gpendingpool.h"
#include "log.h"
using namespace std::chrono_literals;


template <typename T>
class increase_guard final {
    T& iter;
    bool& need_increase;
public:
    increase_guard(T& _iter, bool& _need_increase) : 
        iter(_iter), need_increase(_need_increase) {}
    ~increase_guard(){if (need_increase) {++iter;};};
};
int main()
{

    std::unordered_map<int, int> m{{1, 11}, {2, 22}, {3, 33}, {4,44}, {5,55}, {6,66}};
    for (auto res = m.begin(); res != m.end();) {
        bool increase = true;
        increase_guard guard(res, increase);
        if (res->first % 2 == 0) {
            m.erase(res++);
            increase = false;
        }
    };
    for (auto i: m) {
        std::cout<<i.first<<":"<<i.second<<std::endl;
    }
    //return 0;


    galois::gpendingpool pdp;
    pdp.start();
    while (true) {
        auto res = pdp.ready_queue_pop(1s);
        if (res) {
            galois::gpendingpool::socket_t socket;
            galois::gpendingpool::time_point_t connected_time;
            std::tie(socket, connected_time) = res.value();
            INFO("socket:[%d] poped", socket);
        } else {
            INFO("Time out...", "");
        }
        std::this_thread::sleep_for(std::chrono::seconds(50));
    }
    pdp.stop();
    std::cout<<"exit"<<std::endl;
    return 0;
}