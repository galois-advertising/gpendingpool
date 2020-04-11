#pragma once
#include <string>
#include <atomic>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <list>
#include <queue>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "net.h"

namespace galois {

class gpendingpool {
public: 
    using socket_t = galois::net::socket_t;
    using fd_t = galois::net::fd_t;
    using port_t = int;
    using milliseconds = unsigned int;
    using time_point_t = std::chrono::system_clock::time_point;
    using ready_socket_opt_t = std::optional<std::pair<socket_t, time_point_t>>;
    using socket_opt_t = std::optional<socket_t>;
    using ready_queue_t = std::queue<socket_t>;
public:
    gpendingpool();
    virtual ~gpendingpool();
    gpendingpool(const gpendingpool&) = delete;
    gpendingpool(const gpendingpool&&) = delete;
    gpendingpool& operator= (const gpendingpool&) = delete;

    bool start();
    bool stop();
    void close_listen_fd();
    ready_socket_opt_t ready_queue_pop(const std::chrono::milliseconds& time_out);
protected:
    virtual unsigned int get_listen_port() const;
    virtual unsigned int get_queue_len() const;
    virtual int get_alive_timeout_ms() const;
    virtual int get_queuing_timeout_ms() const;
    virtual int get_select_timeout_ms() const;
    virtual size_t get_max_ready_queue_len() const;

private:
    socket_opt_t tcplisten(port_t, int queue);
private:
    struct fd_item {
        enum class status_t {CONNECTED, queuing};
        status_t status;
        socket_t socket;
        time_point_t connected_time;
        time_point_t enter_queue_time;
        fd_item(socket_t);
        fd_item& operator = (const fd_item&);
        milliseconds alive_time_ms();
        milliseconds queuing_time_ms();
    };
private:
    using socket_to_fd_t = std::unordered_map<socket_t, fd_item>;

    bool ready_queue_push(socket_t);
    bool drop_normal_fd(socket_t);
    int mask_normal_fd(fd_set&);
    bool insert_normal_fd(socket_t); 
    void check_normal_fd(fd_set&);
    void drop_connected_timeout_fd();
private:
    void listen_thread_process();
    const char * get_ip(socket_t fd, char* ipstr, size_t len);
    socket_t listen_fd;
    std::atomic_bool is_exit;
    std::thread listen_thread; 
    std::mutex mtx;
    std::condition_variable cond_var;
    ready_queue_t ready_queue;
    socket_to_fd_t connected_fds;
};

}