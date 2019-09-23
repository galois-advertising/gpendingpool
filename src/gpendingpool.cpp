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

#ifdef _DEBUG_LOG_
#define BEBUG_LOG(fmt, ...) this->log(DEBUG, "["__FILE__"]["__LINE__"][DEBUG]"##fmt, __VA_ARGS__);
#else
#define BEBUG_LOG(fmt, ...)
#endif

#ifdef _NOTICE_LOG_
#define NOTICE_LOG(fmt, ...) this->log(DEBUG, "["__FILE__"]["__LINE__"][DEBUG]"##fmt, __VA_ARGS__);
#else
#define NOTICE_LOG(fmt, ...)
#endif

#ifdef _WARNING_LOG_
#define WARNING_LOG(fmt, ...) this->log(DEBUG, "["__FILE__"]["__LINE__"][DEBUG]"##fmt, __VA_ARGS__);
#else
#define WARNING_LOG(fmt, ...)
#endif

#ifdef _FATAL_LOG_
#define FATAL_LOG(fmt, ...) this->log(DEBUG, "["__FILE__"]["__LINE__"][DEBUG]"##fmt, __VA_ARGS__);
#else
#define FATAL_LOG(fmt, ...)
#endif

namespace galois
{
unsigned int gpendingpool::get_listen_port() const
{
    return 8707;
}

unsigned int gpendingpool::get_queue_len() const
{
    return 5;
}

int gpendingpool::get_alive_timeout_ms() const
{
    return 1024;
}
int gpendingpool::get_select_timeout_ms() const
{
    return 1024;
}

void gpendingpool::log(LOGLEVEL level, const char * fmt, ...) const
{
    char buf[1024];
    va_list args; 
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::cerr<<buf<<std::endl;
}

gpendingpool::gpendingpool() : 
    listen_fd(-1), is_exit(false), listen_thread(), mtx(), cond_var()
{

}

gpendingpool::~gpendingpool()
{

}

int gpendingpool::close_wrap(int fd)
{
    return close(fd);
}

int gpendingpool::listen_wrap(int sockfd, int backlog)
{
    int val = listen(sockfd, backlog);
    if (val == -1) {
        FATAL_LOG("listen(%d,%d) call failed.error[%d] info is %s.", sockfd,
                    backlog, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::setsockopt_wrap(int sockfd, int level, int optname, 
    const void *optval, socklen_t optlen)
{
    int val = setsockopt(sockfd, level, optname, optval, optlen);
    if (val == -1) {
        FATAL_LOG("setsockopt(%d,%d,%d) call failed.error[%d] info is %s.",
                    sockfd, level, optname, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::socket_wrap(int family, int type, int protocol)
{
    int val = socket(family, type, protocol);
    if (val == -1) {
        FATAL_LOG("socket(%d,%d,%d) call failed.error[%d] info is %s.",
                    family, type, protocol, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::bind_wrap(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen)
{
    int val = bind(sockfd, myaddr, addrlen);
    if (val == -1) {
        FATAL_LOG("bind(%d,<%d,%d,%u>,%d) call failed.error[%d] info is %s.",
            sockfd, ((struct sockaddr_in *) myaddr)->sin_family,
            ((struct sockaddr_in *) myaddr)->sin_port,
            ((struct sockaddr_in *) myaddr)->sin_addr.s_addr, addrlen, errno,
            strerror(errno));
    }
    return val;
}

int gpendingpool::tcplisten_wrap(int port, int queue)
{
    int listenfd;
    const int on = 1;
    struct sockaddr_in soin;

    if ((listenfd = socket_wrap(PF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    setsockopt_wrap(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&soin, 0, sizeof(soin));
    soin.sin_family = AF_INET;
    soin.sin_addr.s_addr = htonl(INADDR_ANY);
    soin.sin_port = htons((uint16_t)port);
    if (bind_wrap(listenfd, (struct sockaddr *) &soin, sizeof(soin)) < 0) {
        close_wrap(listenfd);
        return -1;
    }
    if(queue <= 0) {
        queue = 5;
    }
    if (listen_wrap(listenfd, queue) < 0) {
        close_wrap(listenfd);
        return -1;
    }
    return listenfd;
}

int gpendingpool::select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, 
    struct timeval * timeout)
{
    int val;
again:
    val = select(nfds, readfds, writefds, exceptfds, timeout);
    if (val < 0) {
        if (errno == EINTR) {
            goto again;
        }
        FATAL_LOG("select() call error.error[%d] info is %s", errno,
                    strerror(errno));
    }
    if (val == 0) {
        errno = ETIMEDOUT;
    }
    return val;
}

int gpendingpool::accept_wrap(int sockfd, struct sockaddr *sa, socklen_t * addrlen)
{
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
            FATAL_LOG("accept(%d) call failed.error[%d] info is %s.", sockfd,
                        errno, strerror(errno));
            return -1;
        }
    }
    return connfd;
}

void gpendingpool::close_listen_fd()
{
    close_wrap(listen_fd);
    listen_fd = -1;
}

int gpendingpool::getpeername_wrap(int sockfd, struct sockaddr *peeraddr, socklen_t * addrlen)
{
    int val = getpeername(sockfd, peeraddr, addrlen);
    if (val == -1) {
        FATAL_LOG("getpeername(%d) call failed.error[%d] info is %s.\n",
            sockfd, errno, strerror(errno));
    }
    return val;
}

const char * gpendingpool::get_ip(int fd, char* ipstr, size_t len)
{
    if (INET_ADDRSTRLEN > len) {
        return "";
    }
    in_addr in;
    sockaddr_in addr;
    socklen_t addr_len = sizeof(sockaddr_in);
    int ret;
    ret = getpeername_wrap(fd, (sockaddr*)&addr, &addr_len);
    if (ret < 0) {
        FATAL_LOG("getpeername failed, errno=%m");
        return "";
    }
    in.s_addr = addr.sin_addr.s_addr;
    if (inet_ntop(AF_INET, &in, ipstr, INET_ADDRSTRLEN) != NULL) {
        //在ipstr末尾加0，保证字符串打印等
        ipstr[INET_ADDRSTRLEN - 1] = 0;
        return ipstr;
    } else {
        FATAL_LOG("get ip failed, errno=%m");
        return "";
    }
}

void gpendingpool::listen_thread_process()
{

    fd_set fdset;
    timeval select_timeout = {
        get_select_timeout_ms() / 1000, 
        get_select_timeout_ms() * 1000 % 1000000
    };
    int on = 1;
    sockaddr saddr;
    socklen_t addr_len = sizeof(struct sockaddr);
    int accept_fd = -1;
    int max_fd = std::max(mask_item(fdset), listen_fd) + 1;

    while(!is_exit) {
        FD_ZERO(&fdset);
        if (select_wrap(max_fd, &fdset, NULL, NULL, &select_timeout) > 0) {
            if (listen_fd != -1 && FD_ISSET(listen_fd, &fdset)) {
                char ipstr[INET_ADDRSTRLEN];
                if ((accept_fd = accept_wrap(listen_fd, &saddr, &addr_len)) > 0) {
                    int ret_sum = 0;
#ifdef TCP_NODELAY
                    ret_sum += setsockopt(accept_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int));
#endif
#ifdef TCP_QUICKACK
                    ret_sum += setsockopt(accept_fd, IPPROTO_TCP, TCP_QUICKACK, &on, sizeof(int));
#endif
                    if (ret_sum != 0) { 
                        FATAL_LOG("set socket option error");
                    }
                    const char * ip_address = get_ip(accept_fd, ipstr, INET_ADDRSTRLEN);
                    if (!insert_item(accept_fd)) {
                        FATAL_LOG("UI connect overflow! ip=%s.sock num=%d", ip_address);
                        close_wrap(accept_fd);
                    } else {
                        NOTICE_LOG("accept connect from %s.", ip_address);
                    }
                } else {
                    WARNING_LOG("accept request from UI error! ret=%d", accept_fd);
                    continue;
                }
            }
            check_item(fdset);
        }
    }
}


bool gpendingpool::insert_item(int socket)
{
    // Insert a accept fd 
    auto iter = fd_items.find(socket);
    if (iter != fd_items.end()) {
        return false;
    }
    fd_items.insert(std::make_pair(socket, fd_item(fd_item::READY, socket)));
    return true;
}

int gpendingpool::mask_item(fd_set & pfs)
{
    int max_fd = 0;
    int ready_cnt = 0;
    int busy_cnt = 0;
    std::for_each(fd_items.begin(), fd_items.end(),
        [&pfs, &max_fd, &ready_cnt, &busy_cnt](auto & fd){
            switch(fd.second.status)
            {
            case fd_item::READY:
                FD_SET(fd.first, &pfs);
                max_fd = std::max(max_fd, fd.first);
                ready_cnt++; 
            break;
            case fd_item::BUSY:
                busy_cnt++;
            break;
            default:
            break;
            }
    });
    return max_fd; 
}

bool gpendingpool::reset_item(int socket, bool bKeepAlive)
{
    auto iter = fd_items.find(socket);
    if (iter == fd_items.end()) {
        return false;
    } 
    if (!bKeepAlive) {
        fd_items.erase(socket);
        close_wrap(socket);
    } else {
        if (iter->second.status == fd_item::BUSY) {
            iter->second.status = fd_item::READY;
        }
    }
    return true;
}

void gpendingpool::check_item(fd_set & pfs)
{
    std::for_each(fd_items.begin(), fd_items.end(), [this, &pfs](auto & fd){
        switch (fd.second.status)
        {
        case fd_item::BUSY:
            fd.second.last_active = std::chrono::system_clock::now();
        break;
        case fd_item::READY:
            if (FD_ISSET(fd.first, &pfs)) {
                if (ready_queue_push(fd.first) < 0) {
                    reset_item(fd.first, false);
                } else {
                    auto td = std::chrono::system_clock::now() - fd.second.last_active;
                    auto ms_cnt = std::chrono::duration_cast<std::chrono::milliseconds>(td).count();
                    if (ms_cnt > get_alive_timeout_ms() ) {
                        WARNING_LOG("[get query] socket %d, handle %d timeout", 
                            fd.first, ms_cnt);
                        reset_item(fd.first, false);
                    }
                }
            }
        break;
        default:break;
        }
    });
}

bool gpendingpool::ready_queue_push(int socket)
{
    std::lock_guard<std::mutex> lk(mtx);
    auto iter = fd_items.find(socket);
    if (iter == fd_items.end())
        return false;
    iter->second.last_active = std::chrono::system_clock::now();
    iter->second.enter_queue_time = std::chrono::system_clock::now();

    if (ready_queue.size() > 100) {
        WARNING_LOG("Buffer overflow: %u", ready_queue.size());
        return false;
    } else 
    {
        iter->second.status = fd_item::BUSY;
        ready_queue.push(iter->first);
        cond_var.notify_one();
        log(DEBUG,
            "Ready %u sockets: handle %d, signal sent.", 
            ready_queue.size(), iter->first);
    }
    return true;
}

std::optional<std::pair<int, unsigned int>> gpendingpool::ready_queue_pop()
{
    std::unique_lock<std::mutex> lk(mtx);
    cond_var.wait(lk, [this]{return !this->ready_queue.empty();});
    int socket = ready_queue.front();
    ready_queue.pop();
    auto iter = fd_items.find(socket);
    if (iter == fd_items.end()) {
        return {};
    }
    else {
        iter->second.status = fd_item::BUSY;
    }
    auto wait_time = std::chrono::system_clock::now() - iter->second.enter_queue_time;
    unsigned int ms_cnt = std::chrono::duration_cast<std::chrono::microseconds>(wait_time).count();
    return std::make_pair(socket, ms_cnt);
}

bool gpendingpool::start()
{
    log(DEBUG, "start");
    if (is_exit) {
        WARNING_LOG("has STOP");
        return false;
    }
    listen_fd = -1;
    if ((listen_fd = tcplisten_wrap(get_listen_port(), get_queue_len())) < 0) {
        WARNING_LOG("fail to listen [port=%u]!", get_listen_port());
        return false;
    }
    else {
        WARNING_LOG("succ to listen port[%d] on fd[%d]", 
            get_listen_port(), listen_fd);
    }
    listen_thread = std::thread([this]{this->listen_thread_process();});
    return true;

}

bool gpendingpool::stop()
{
    log(DEBUG, "stop");
    is_exit = true;
    this->listen_thread.join();
    return true;
}
}