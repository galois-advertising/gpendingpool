#include <iostream>
#include "gpendingpool.h"
#include <thread>


int main()
{
    galois::gpendingpool pdp;
    pdp.start();
    std::this_thread::sleep_for(std::chrono::seconds(1000));
    pdp.stop();
    std::cout<<"exit"<<std::endl;
    return 0;
}