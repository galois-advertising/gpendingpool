#include <stdarg.h>
#include "pendingpool.h"
#include <iostream>


namespace galois
{
unsigned int pendingpool::get_listen_port() const
{
    return 8707;
}
unsigned int pendingpool::get_alive_timeout() const
{
    return 1000;
}
unsigned int pendingpool::get_select_timeout() const
{
    return 1000;
}
void pendingpool::log(const char * fmt, ...) const
{
    char buf[1024];
    va_list args; 
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::cerr<<buf<<std::endl;
}

pendingpool::pendingpool()
{

}

pendingpool::~pendingpool()
{

}

bool pendingpool::start()
{
    log("start");
    return true;

}

bool pendingpool::stop()
{
    log("stop");
    return true;
}
}