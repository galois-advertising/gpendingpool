#pragma once
#include <string>
#include <thread>
#include <atomic>

namespace galois
{

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
    virtual void log(const char * fmt, ...) const;
private:
    std::atomic_bool _is_exit {false};
    
    
    
};

}