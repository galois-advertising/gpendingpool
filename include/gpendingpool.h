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

enum class LOGLEVEL {FATAL, WARNING, NOTICE, DEBUG};

class gpendingpool
{
public: 
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
    int tcplisten_wrap(int port, int queue);
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
        chrono::system_clock::time_point last_active;
        chrono::system_clock::time_point enter_queue_time;
        fd_item(decltype(status) _status, int _socket) : 
            status(_status), socket(_socket), 
            last_active(chrono::system_clock::now()), enter_queue_time() {}
    };
    std::map<int, fd_item> fd_items;
    // Insert the socket into readyqueue
    bool ready_queue_push(int socket);
    // Get a READY socket and mark it to BUSY. (out,out,out)
<<<<<<< HEAD
    std::optional<std::pair<int, std::chrono::duration>> ready_queue_pop();
    // Close handle.(Just set to BUSY if bKeepAlive)
    bool reset_item(int socket, bool bKeepAlive);
    // Add the fds which are READY to fd_set 
    int mask_item(fd_set & pfs);
=======
    bool work_fetch_item(int &handle, int &sock, int &wait);
    // Close handle.(Just set to BUSY if bKeepAlive)
    void work_reset_item(int handle, bool bKeepAlive); 
    // Add the fds which are READY to fd_set 
    int mask(fd_set * pfs);
>>>>>>> 2847ad0308c6eb1f8fe39b82e749d1295e45c6c3
    // Insert a accept fd 
    int insert_item(int sock_work); 
    // Check all sockets.Only update the active time for socket which are BUSY, 
    // or add to ready queue if the socket is READY and has been set.
<<<<<<< HEAD
    void check_item(fd_set & pfs);
=======
    void check_item(fd_set * pfs);
    // Insert the socket into readyqueue
    int queue_in(int offset);
    void set_timeout(int sec);    
    // Set length of readyqueue.
    // Be careful: this function must be called before other function. 
    // And you cannnot set it dynamicily.
    void set_queuelen(int len);    
    // Set length of socknum.
    // Be careful: this function must be called before other function. 
    // And you cannnot set it dynamicily.
    void set_socknum(int num);    
    int get_freethread();    
    int get_queuelen();
>>>>>>> 2847ad0308c6eb1f8fe39b82e749d1295e45c6c3
private:
    void listen_thread_process();
    const char * get_ip(int fd, char* ipstr, size_t len);
    int listen_fd;
    std::atomic_bool is_exit;
    std::thread listen_thread; 
    std::mutex mtx;
    std::condition_variable cond_var;
    std::queue<int> ready_queue;
};

}