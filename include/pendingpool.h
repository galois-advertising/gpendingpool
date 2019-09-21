#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>

namespace galois
{

enum class LOGLEVEL {FATAL, WARNING, NOTICE, DEBUG};

class pendingpool
{
public: 
    pendingpool();
    ~pendingpool();
    bool start();
    bool stop();
protected:
    virtual unsigned int get_listen_port() const;
    virtual unsigned int get_alive_timeout() const;
    virtual unsigned int get_select_timeout() const;
    virtual void log(LOGLEVEL level, const char * fmt, ...) const;
private:
    void listen_thread();
    int select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, timeval *timeout);
    std::atomic_bool _is_exit {false};
    
    
    
};

}