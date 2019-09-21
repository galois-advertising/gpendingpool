#include <stdarg.h>
#include "pendingpool.h"


namespace galois::pendingpool
{
unsigned int pendingpool::get_listen_port()
{
    return 8707;
}
unsigned int pendingpool::get_alive_timeout()
{
    return 1000;
}
unsigned int pendingpool::get_select_timeout()
{
    return 1000;
}
void pendingpool::log(char * fmt, ...)
{
    char buf[1024];
    va_list args; 
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::cerr<<buf<<std::endl;
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