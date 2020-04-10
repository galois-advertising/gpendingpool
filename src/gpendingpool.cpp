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


template <typename T>
class increase_guard final {
    T& iter;
    bool& need_increase;
public:
    increase_guard(T& _iter, bool& _need_increase) : 
        iter(_iter), need_increase(_need_increase) {}
    ~increase_guard(){if (need_increase) {++iter;};};
};

unsigned int gpendingpool::get_listen_port() const {
    return 8709;
}

unsigned int gpendingpool::get_queue_len() const {
    return 5;
}

int gpendingpool::get_alive_timeout_ms() const {
    return 10096;
}

int gpendingpool::get_queuing_timeout_ms() const {
    return 20000;
}

int gpendingpool::get_select_timeout_ms() const {
    return 2048;
}

size_t gpendingpool::get_max_ready_queue_len() const {
    return 3;
}

gpendingpool::gpendingpool() : 
    listen_fd(-1), is_exit(false), listen_thread(), mtx(), cond_var() {
}

gpendingpool::~gpendingpool() {
}

gpendingpool::fd_item::fd_item(socket_t _socket) : 
    status(gpendingpool::fd_item::status_t::CONNECTED), socket(_socket), 
    connected_time(std::chrono::system_clock::now()), enter_queue_time() {
}

gpendingpool::fd_item & gpendingpool::fd_item::operator = (const fd_item & o) {
    socket = o.socket;
    connected_time = o.connected_time;
    enter_queue_time = o.enter_queue_time;
    return *this;
}

gpendingpool::milliseconds gpendingpool::fd_item::alive_time_ms() {
    auto wait_time = std::chrono::system_clock::now() - connected_time;
    return std::chrono::duration_cast<std::chrono::milliseconds>(wait_time).count();
}

gpendingpool::milliseconds gpendingpool::fd_item::queuing_time_ms() {
    auto wait_time = std::chrono::system_clock::now() - enter_queue_time;
    return std::chrono::duration_cast<std::chrono::milliseconds>(wait_time).count();
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
    TRACE("[gpendingpool] listen thread start.", "");
    fd_set fdset;
    timeval select_timeout = {
        get_select_timeout_ms() / 1000, 
        get_select_timeout_ms() * 1000 % 1000000
    };
    sockaddr saddr;
    socklen_t addr_len = sizeof(sockaddr);

    while(!is_exit) {
        FD_ZERO(&fdset);
        // 1.set listen_fd
        if (listen_fd != -1) {
            FD_SET(listen_fd, &fdset);
        };
        // 2.set other fd && get max_fd
        fd_t max_fd = std::max(mask_normal_fd(fdset), listen_fd) + 1;
        TRACE("[gpendingpool] max_fd [%d].", max_fd);
        if (auto select_res = select_wrap(max_fd, &fdset, NULL, NULL, &select_timeout); select_res < 0) {
            FATAL("[gpendingpool] select error: %d", errno);
        } else if (select_res == 0) {
            drop_connected_timeout_fd();
        } else if (select_res > 0) {
             TRACE("[gpendingpool] select_res > 0.", "");
            // 1.check listen_fd
            if (listen_fd != -1 && FD_ISSET(listen_fd, &fdset)) {
                if (fd_t accept_fd = accept_wrap(listen_fd, &saddr, &addr_len); accept_fd > 0) {
                    TRACE("[gpendingpool] accept a new fd: [%d]", accept_fd);
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
                    if (insert_normal_fd(accept_fd)) {
                        char ipstr[INET_ADDRSTRLEN];
                        auto ip_address = get_ip(accept_fd, ipstr, INET_ADDRSTRLEN);
                        INFO("[gpendingpool] accept connect from [%d][%s].", accept_fd, ip_address);
                    } else {
                        close_wrap(accept_fd);
                    }
                } else {
                    WARNING("[gpendingpool] accept request from UI error! ret=%d", accept_fd);
                }
            }
            // 2.check other fd
            check_normal_fd(fdset);
        }
    }
}

bool gpendingpool::insert_normal_fd(socket_t socket) {
    // Insert a accept fd 
    auto res = connected_fds.insert(std::make_pair(socket, fd_item(socket)));
    if (res.second) {
        TRACE("[gpendingpool] insert_normal_fd [%d] succeed.", socket);
        return true;
    }
    FATAL("[gpendingpool] insert_normal_fd [%d] fail: already exists.", socket);
    return false;
}

int gpendingpool::mask_normal_fd(fd_set & pfs) {
    int max_fd = 0;
    std::for_each(connected_fds.begin(), connected_fds.end(),
        [&pfs, &max_fd](auto & fd){
            if (fd.second.status == gpendingpool::fd_item::status_t::CONNECTED) {
                FD_SET(fd.first, &pfs);
                max_fd = std::max(max_fd, fd.first);
            }
    });
    return max_fd; 
}

bool gpendingpool::drop_normal_fd(int socket) {
    if (auto iter = connected_fds.find(socket); iter != connected_fds.end()) {
        connected_fds.erase(socket);
        close_wrap(socket);
        TRACE("[gpendingpool] drop_normal_fd[%d]", socket);
        return true;
    } 
    return false;
}

void gpendingpool::drop_connected_timeout_fd() {
    TRACE("[gpendingpool]{drop_connected_timeout_fd} >>>>>>>>>>>", "");
    for (auto pos = connected_fds.begin(); pos != connected_fds.end();) {
        bool need_increase = true;
        increase_guard guard(pos, need_increase);
        if (pos->second.status == fd_item::status_t::CONNECTED) {
            if (auto pending_time = pos->second.alive_time_ms(); pending_time > get_alive_timeout_ms()) {
                TRACE("[gpendingpool]{drop_connected_timeout_fd}[%d] timeout [%u] > [%u]", 
                    pos->first, pending_time, get_alive_timeout_ms());
                close_wrap(pos->first);
                connected_fds.erase(pos++);
                need_increase = false;
            } else {
                TRACE("[gpendingpool]{drop_connected_timeout_fd}[%d] continue [%u] <= [%u]", 
                    pos->first, pending_time, get_alive_timeout_ms());
            } 
        } else {
            TRACE("[gpendingpool]{drop_connected_timeout_fd}[%d] queuing:ignored", pos->first);
        }
    };
    TRACE("[gpendingpool]{drop_connected_timeout_fd} <<<<<<<<<<<", "");
}

void gpendingpool::check_normal_fd(fd_set & pfs) {
    TRACE("[gpendingpool] check_normal_fd >>>>>>>>>>", "");
    for (auto pos = connected_fds.begin(); pos != connected_fds.end();) {
        bool need_increase = true;
        increase_guard guard(pos, need_increase);
        if (FD_ISSET(pos->first, &pfs)) {
            TRACE("[gpendingpool] check_normal_fd [%d] is set", pos->first);
            if (pos->second.status != fd_item::status_t::queuing) {
                TRACE("[gpendingpool] ready_queue_push", "");
                if (!ready_queue_push(pos->first)) {
                    close_wrap(pos->first);
                    connected_fds.erase(pos++);
                    need_increase = false;
                } 
            } 
        } 
    };
    TRACE("[gpendingpool] check_normal_fd <<<<<<<<<<", "");
}

bool gpendingpool::ready_queue_push(int socket) {
    std::lock_guard<std::mutex> lk(mtx);
    auto iter = connected_fds.find(socket);
    if (iter == connected_fds.end())
        return false;
    iter->second.enter_queue_time = std::chrono::system_clock::now();

    if (ready_queue.size() >= get_max_ready_queue_len()) {
        WARNING("[gpendingpool] Buffer overflow: %u", get_max_ready_queue_len());
        return false;
    } else {
        iter->second.status = fd_item::status_t::queuing;
        ready_queue.push(iter->first);
        cond_var.notify_one();
        TRACE("[gpendingpool] Ready %u sockets: handle %d, signal sent.", 
            ready_queue.size(), iter->first);
    }
    return true;
}

gpendingpool::ready_socket_opt_t gpendingpool::ready_queue_pop(const std::chrono::milliseconds& time_out) {
    std::unique_lock<std::mutex> lk(mtx);
    if (!cond_var.wait_for(lk, time_out, [this]{return !this->ready_queue.empty();})) {
        return std::nullopt;
    }
    while (!ready_queue.empty()) {
        auto socket = ready_queue.front();
        ready_queue.pop();
        if (auto iter = connected_fds.find(socket); iter != connected_fds.end()) {
            if (auto queuing_time = iter->second.queuing_time_ms(); queuing_time > get_queuing_timeout_ms()) {
                close_wrap(iter->first);
                connected_fds.erase(iter);
                WARNING("[gpendingpool]{ready_queue_pop}[%d] queuing time out [%u] >[%u]", 
                    socket, queuing_time, get_queuing_timeout_ms());
            } else {
                connected_fds.erase(iter);
                return std::make_pair(socket, iter->second.connected_time);
            }
        } else {
            FATAL("[gpendingpool]{ready_queue_pop}[%d] should be here", iter->first);
        } 
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