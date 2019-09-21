#include <stdarg.h>
#include "pendingpool.h"
#include <iostream>
#include <unistd.h>


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
void pendingpool::log(LOGLEVEL level, const char * fmt, ...) const
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

int pendingpool::select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds,
          timeval *timeout)
{
    int val;
again:
    val = select(nfds, readfds, writefds, exceptfds, timeout);
    if (val < 0) {
        if (errno == EINTR) {
            if (timeout != NULL) {
            }
            goto again;
        }
        log(LOGLEVEL::WARNING, "select() call error.error[%d] info is %s", errno,
                    strerror(errno));
    }
    if (val == 0) {
        errno = ETIMEDOUT;
    }
    return val;
}

void pendingpool::listen_thread()
{

    while(!_is_exit) {


    }
}

bool pendingpool::start()
{
    log(LOGLEVEL::DEBUG, "start");
    return true;

}

bool pendingpool::stop()
{
    log(LOGLEVEL::DEBUG, "stop");
    return true;
}
}