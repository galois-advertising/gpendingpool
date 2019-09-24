#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <list>
#include <queue>
#include <map>

namespace galois
{


enum LOGLEVEL {FATAL, WARNING, NOTICE, DEBUG};
class gpendingpool
{
public: 
    typedef int socket_t;
    gpendingpool();
    ~gpendingpool();
    bool start();
    bool stop();
    void close_listen_fd();
protected:
    virtual unsigned int get_listen_port() const;
    virtual unsigned int get_queue_len() const;
    virtual int get_alive_timeout_ms() const;
    virtual int get_select_timeout_ms() const;
    virtual void log(LOGLEVEL level, const char * fmt, ...) const;
private:
    // function wraps
    int select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, struct timeval *timeout);
    int accept_wrap(int sockfd, struct sockaddr * sa, socklen_t * addrlen);
    int listen_wrap(int sockfd, int backlog);
    std::optional<socket_t> tcplisten_wrap(int port, int queue);
    int close_wrap(int fd);
    int getpeername_wrap(int sockfd, struct sockaddr * peeraddr, socklen_t * addrlen);
    int setsockopt_wrap(int sockfd, int level, int optname, const void * optval, socklen_t optlen);
    int socket_wrap(int family, int type, int protocol);
    int bind_wrap(int sockfd, const struct sockaddr * myaddr, socklen_t addrlen);
    //
private:
    struct fd_item
    {
        enum {READY, BUSY} status;
        int socket;
        std::chrono::system_clock::time_point last_active;
        std::chrono::system_clock::time_point enter_queue_time;
        fd_item(decltype(status) _status, socket_t _socket) : 
            status(_status), socket(_socket), 
            last_active(std::chrono::system_clock::now()), enter_queue_time() {}
        fd_item & operator = (const fd_item & o) {
            status = o.status; 
            socket = o.socket;
            last_active = o.last_active;
            enter_queue_time = o.enter_queue_time;
            return *this;
        }
    };
    std::map<socket_t, fd_item> fd_items;
    bool ready_queue_push(socket_t socket);
    std::optional<std::pair<socket_t, unsigned int> > ready_queue_pop();
    bool reset_item(socket_t socket, bool bKeepAlive);
    int mask_item(fd_set & pfs);
    bool insert_item(socket_t socket); 
    void check_item(fd_set & pfs);
private:
    void listen_thread_process();
    const char * get_ip(socket_t fd, char* ipstr, size_t len);
    socket_t listen_fd;
    std::atomic_bool is_exit;
    std::thread listen_thread; 
    std::mutex mtx;
    std::condition_variable cond_var;
    std::queue<socket_t> ready_queue;
};

}