#include <iostream>
#include <algorithm>
#include <optional>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>  
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "gpendingpool.h"
#include <chrono>
#include "log.h"

namespace galois {
unsigned int gpendingpool::get_listen_port() const {
    return 8709;
}

unsigned int gpendingpool::get_queue_len() const {
    return 5;
}

int gpendingpool::get_alive_timeout_ms() const {
    return 4096;
}

int gpendingpool::get_select_timeout_ms() const {
    return 1024;
}

size_t gpendingpool::get_max_ready_queue_len() const {
    return 3;
}

gpendingpool::gpendingpool() : 
    listen_fd(-1), is_exit(false), listen_thread(), mtx(), cond_var() {
}

gpendingpool::~gpendingpool() {
}

gpendingpool::fd_item::fd_item(status_t _status, socket_t _socket) : 
    status(_status), socket(_socket), 
    last_active(std::chrono::system_clock::now()), enter_queue_time() {

}

gpendingpool::fd_item & gpendingpool::fd_item::operator = (const fd_item & o) {
    status = o.status; 
    socket = o.socket;
    last_active = o.last_active;
    enter_queue_time = o.enter_queue_time;
    return *this;
}

int gpendingpool::close_wrap(fd_t fd) {
    return close(fd);
}

int gpendingpool::listen_wrap(fd_t sockfd, int backlog) {
    int val = listen(sockfd, backlog);
    if (val == -1) {
        FATAL("[gpendingpool] listen(%d,%d) call failed.error[%d] info is %s.", sockfd,
            backlog, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::setsockopt_wrap(fd_t sockfd, int level, int optname, 
    const void *optval, socklen_t optlen) {
    int val = setsockopt(sockfd, level, optname, optval, optlen);
    if (val == -1) {
        FATAL("[gpendingpool] setsockopt(%d,%d,%d) call failed.error[%d] info is %s.",
                    sockfd, level, optname, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::socket_wrap(int family, int type, int protocol) {
    int val = socket(family, type, protocol);
    if (val == -1) {
        FATAL("[gpendingpool] socket(%d,%d,%d) call failed.error[%d] info is %s.",
                    family, type, protocol, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::bind_wrap(fd_t sockfd, const sockaddr* myaddr, socklen_t addrlen) {
    int val = bind(sockfd, myaddr, addrlen);
    if (val == -1) {
        FATAL("[gpendingpool] bind(%d,<%d,%d,%u>,%d) call failed.error[%d] info is %s.",
            sockfd, reinterpret_cast<const sockaddr_in*>(myaddr)->sin_family,
            reinterpret_cast<const sockaddr_in*>(myaddr)->sin_port,
            reinterpret_cast<const sockaddr_in*>(myaddr)->sin_addr.s_addr, addrlen, errno,
            strerror(errno));
    }
    return val;
}

gpendingpool::socket_opt_t gpendingpool::tcplisten(port_t port, int queue) {
    socket_t listenfd;
    const int on = 1;
    struct sockaddr_in soin;

    if ((listenfd = socket_wrap(PF_INET, SOCK_STREAM, 0)) < 0) {
        return std::nullopt;
    }
    setsockopt_wrap(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&soin, 0, sizeof(soin));
    soin.sin_family = AF_INET;
    soin.sin_addr.s_addr = htonl(INADDR_ANY);
    soin.sin_port = htons((uint16_t)port);
    if (bind_wrap(listenfd, (struct sockaddr *) &soin, sizeof(soin)) < 0) {
        close_wrap(listenfd);
        return std::nullopt;
    }
    if(queue <= 0) {
        queue = 5;
    }
    if (listen_wrap(listenfd, queue) < 0) {
        close_wrap(listenfd);
        return std::nullopt;
    }
    return listenfd;
}

int gpendingpool::select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, 
    struct timeval * timeout) {
    int val;
again:
    val = select(nfds, readfds, writefds, exceptfds, timeout);
    if (val < 0) {
        if (errno == EINTR) {
            goto again;
        }
        FATAL("[gpendingpool] select() call error.error[%d] info is %s", errno,
                    strerror(errno));
    }
    if (val == 0) {
        errno = ETIMEDOUT;
    }
    return val;
}

int gpendingpool::accept_wrap(fd_t sockfd, struct sockaddr *sa, socklen_t * addrlen) {
    int connfd = 0;
again:
    connfd = accept(sockfd, sa, addrlen);
    if (connfd < 0) {
#ifdef  EPROTO
        if (errno == EPROTO || errno == ECONNABORTED) {
#else
        if (errno == ECONNABORTED) {
#endif
            goto again;
        } else {
            FATAL("[gpendingpool] accept(%d) call failed.error[%d] info is %s.", sockfd,
                        errno, strerror(errno));
            return -1;
        }
    }
    return connfd;
}

void gpendingpool::close_listen_fd() {
    close_wrap(listen_fd);
    listen_fd = -1;
}

int gpendingpool::getpeername_wrap(fd_t sockfd, struct sockaddr *peeraddr, socklen_t * addrlen) {
    int val = getpeername(sockfd, peeraddr, addrlen);
    if (val == -1) {
        FATAL("[gpendingpool] getpeername(%d) call failed.error[%d] info is %s.\n",
            sockfd, errno, strerror(errno));
    }
    return val;
}

const char * gpendingpool::get_ip(int fd, char* ipstr, size_t len) {
    if (INET_ADDRSTRLEN > len) {
        return "";
    }
    in_addr in;
    sockaddr_in addr;
    socklen_t addr_len = sizeof(sockaddr_in);
    int ret;
    ret = getpeername_wrap(fd, (sockaddr*)&addr, &addr_len);
    if (ret < 0) {
        FATAL("[gpendingpool] getpeername failed, errno=%m", errno);
        return "";
    }
    in.s_addr = addr.sin_addr.s_addr;
    if (inet_ntop(AF_INET, &in, ipstr, INET_ADDRSTRLEN) != NULL) {
        ipstr[INET_ADDRSTRLEN - 1] = 0;
        return ipstr;
    } else {
        FATAL("[gpendingpool] get ip failed, errno=%m", errno);
        return "";
    }
}

void gpendingpool::listen_thread_process() {
    TRACE("[gpendingpool] listen thread start.%s", "");
    fd_set fdset;
    timeval select_timeout = {
        get_select_timeout_ms() / 1000, 
        get_select_timeout_ms() * 1000 % 1000000
    };
    sockaddr saddr;
    socklen_t addr_len = sizeof(struct sockaddr);
    int accept_fd = -1;

    while(!is_exit) {
        FD_ZERO(&fdset);
        if (listen_fd != -1) {
            FD_SET(listen_fd, &fdset);
        };
        int max_fd = std::max(mask_item(fdset), listen_fd) + 1;
        TRACE("[gpendingpool] max_fd:%d", max_fd);
        auto select_res = select_wrap(max_fd, &fdset, NULL, NULL, &select_timeout);
        if (select_res < 0) {
            FATAL("[gpendingpool] select error: %d", errno);
        } else if (select_res == 0) {
            TRACE("[gpendingpool] fd size: %u", fd_items.size());
        } else if (select_res > 0) {
            if (listen_fd != -1 && FD_ISSET(listen_fd, &fdset)) {
                char ipstr[INET_ADDRSTRLEN];
                if ((accept_fd = accept_wrap(listen_fd, &saddr, &addr_len)) > 0) {
                    TRACE("[gpendingpool] accept a new fd: %d", accept_fd);
                    int ret_sum = 0;
                    int on = 1;
#ifdef TCP_NODELAY
                    ret_sum += setsockopt(accept_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int));
#endif
#ifdef TCP_QUICKACK
                    ret_sum += setsockopt(accept_fd, IPPROTO_TCP, TCP_QUICKACK, &on, sizeof(int));
#endif
                    if (ret_sum != 0) { 
                        FATAL("[gpendingpool] set socket option error[ret_sum:%d]", ret_sum);
                    }
                    const char * ip_address = get_ip(accept_fd, ipstr, INET_ADDRSTRLEN);
                    if (!insert_item(accept_fd)) {
                        close_wrap(accept_fd);
                    } else {
                        INFO("[gpendingpool] accepconnectt  from %s.", ip_address);
                    }
                } else {
                    WARNING("[gpendingpool] accept request from UI error! ret=%d", accept_fd);
                    continue;
                }
            }
            check_item(fdset);
        }
    }
}


bool gpendingpool::insert_item(int socket) {
    // Insert a accept fd 
    auto res = fd_items.insert(std::make_pair(socket, fd_item(fd_item::status_t::READY, socket)));
    if (res.second) {
        TRACE("[gpendingpool] insert_item succeed: fd[%d].", socket);
        return true;
    }
    FATAL("[gpendingpool] insert_item fail: fd[%d] already exists.", socket);
    return true;
}

int gpendingpool::mask_item(fd_set & pfs) {
    int max_fd = 0;
    int ready_cnt = 0;
    int busy_cnt = 0;
    std::for_each(fd_items.begin(), fd_items.end(),
        [&pfs, &max_fd, &ready_cnt, &busy_cnt](auto & fd){
            switch(fd.second.status)
            {
            case fd_item::status_t::READY:
                FD_SET(fd.first, &pfs);
                max_fd = std::max(max_fd, fd.first);
                ready_cnt++; 
            break;
            case fd_item::status_t::BUSY:
                busy_cnt++;
            break;
            default:
            break;
            }
    });
    return max_fd; 
}

bool gpendingpool::reset_item(int socket, bool bKeepAlive) {
    auto iter = fd_items.find(socket);
    if (iter == fd_items.end()) {
        return false;
    } 
    if (!bKeepAlive) {
        fd_items.erase(socket);
        close_wrap(socket);
    } else {
        if (iter->second.status == fd_item::status_t::BUSY) {
            iter->second.status = fd_item::status_t::READY;
        }
    }
    return true;
}

void gpendingpool::check_item(fd_set & pfs) {
    std::for_each(fd_items.begin(), fd_items.end(), [this, &pfs](auto & fd) {
        switch (fd.second.status)
        {
        case fd_item::status_t::BUSY:
            fd.second.last_active = std::chrono::system_clock::now();
        break;
        case fd_item::status_t::READY:
            if (FD_ISSET(fd.first, &pfs)) {
                if (ready_queue_push(fd.first) < 0) {
                    reset_item(fd.first, false);
                } else {
                    auto td = std::chrono::system_clock::now() - fd.second.last_active;
                    auto ms_cnt = std::chrono::duration_cast<std::chrono::milliseconds>(td).count();
                    if (ms_cnt > get_alive_timeout_ms()) {
                        WARNING("[gpendingpool] socket %d[%u] timeout[%u]", fd.first, ms_cnt, get_alive_timeout_ms());
                        reset_item(fd.first, false);
                    }
                }
            }
        break;
        default:break;
        }
    });
}

bool gpendingpool::ready_queue_push(int socket) {
    std::lock_guard<std::mutex> lk(mtx);
    auto iter = fd_items.find(socket);
    if (iter == fd_items.end())
        return false;
    iter->second.last_active = std::chrono::system_clock::now();
    iter->second.enter_queue_time = std::chrono::system_clock::now();

    if (ready_queue.size() >= get_max_ready_queue_len()) {
        WARNING("[gpendingpool] Buffer overflow: %u", get_max_ready_queue_len());
        return false;
    } else {
        iter->second.status = fd_item::status_t::BUSY;
        ready_queue.push(iter->first);
        cond_var.notify_one();
        TRACE("[gpendingpool] Ready %u sockets: handle %d, signal sent.", 
            ready_queue.size(), iter->first);
    }
    return true;
}

gpendingpool::ready_socket_opt_t gpendingpool::ready_queue_pop(const std::chrono::milliseconds& time_out) {
    std::unique_lock<std::mutex> lk(mtx);
    if(!cond_var.wait_for(lk, time_out, [this]{return !this->ready_queue.empty();})) {
        return std::nullopt;
    }
    auto socket = ready_queue.front();
    ready_queue.pop();
    auto iter = fd_items.find(socket);
    if (iter != fd_items.end()) {
        iter->second.status = fd_item::status_t::BUSY;
        auto wait_time = std::chrono::system_clock::now() - iter->second.enter_queue_time;
        unsigned int ms_cnt = std::chrono::duration_cast<std::chrono::milliseconds>(wait_time).count();
        return std::make_pair(socket, ms_cnt);
    } 
    return std::nullopt;
}

bool gpendingpool::start() {
    TRACE("[gpendingpool] start: %s", "gpendingpool");
    if (is_exit) {
        return false;
    }
    listen_fd = -1;
    auto lfd = tcplisten(get_listen_port(), get_queue_len());
    if (lfd) {
        listen_fd = lfd.value();
        listen_thread = std::thread([this]{this->listen_thread_process();});
        WARNING("[gpendingpool] succ to listen port[%d] on fd[%d]", 
            get_listen_port(), listen_fd);
        return true;
    }
    WARNING("[gpendingpool] fail to listen [port=%u]!", get_listen_port());
    return false;

}

bool gpendingpool::stop() {
    TRACE("[gpendingpool] stop: %s", "gpendingpool");
    is_exit = true;
    this->listen_thread.join();
    return true;
}
}