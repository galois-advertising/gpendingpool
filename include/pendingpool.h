#include <string>


class pendingpool
{
public: 
    virtual unsigned int get_listen_port() = 0;
    virtual unsigned int get_alive_timeout() = 0;
    virtual unsigned int get_select_timeout() = 0;
    virtual void log(char * fmt, ...) = 0;


};