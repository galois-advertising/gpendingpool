#include <stdarg.h>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "gpendingpool.h"


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

gpendingpool::gpendingpool() : listen_fd(-1), is_exit(false), listen_thread()
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
        log(LOGLEVEL::FATAL, "listen(%d,%d) call failed.error[%d] info is %s.", sockfd,
                    backlog, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::setsockopt_wrap(int sockfd, int level, int optname, 
    const void *optval, socklen_t optlen)
{
    int val = setsockopt(sockfd, level, optname, optval, optlen);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "setsockopt(%d,%d,%d) call failed.error[%d] info is %s.",
                    sockfd, level, optname, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::socket_wrap(int family, int type, int protocol)
{
    int val = socket(family, type, protocol);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "socket(%d,%d,%d) call failed.error[%d] info is %s.",
                    family, type, protocol, errno, strerror(errno));
    }
    return val;
}

int gpendingpool::bind_wrap(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen)
{
    int val = bind(sockfd, myaddr, addrlen);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "bind(%d,<%d,%d,%u>,%d) call failed.error[%d] info is %s.",
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
        log(LOGLEVEL::FATAL, "select() call error.error[%d] info is %s", errno,
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
            log(LOGLEVEL::FATAL, "accept(%d) call failed.error[%d] info is %s.", sockfd,
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
        log(LOGLEVEL::FATAL, "getpeername(%d) call failed.error[%d] info is %s.\n",
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
    ret = ul_getpeername(fd, (sockaddr*)&addr, &addr_len);
    if (ret < 0) {
        log(LOGLEVEL::FATAL, "getpeername failed, errno=%m");
        return "";
    }
    in.s_addr = addr.sin_addr.s_addr;
    if (inet_ntop(AF_INET, &in, ipstr, INET_ADDRSTRLEN) != NULL) {
        //��ipstrĩβ��0����֤�ַ�����ӡ��
        ipstr[INET_ADDRSTRLEN - 1] = 0;
        return ipstr;
    } else {
        log(LOGLEVEL::FATAL, "get ip failed, errno=%m");
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
    int max_fd = 0;
    int on = 1;
    sockaddr saddr;
    socklen_t addr_len = sizeof(struct sockaddr);
    int accept_fd = -1;
    while(!is_exit) {
        FD_ZERO(&fdset);
        if (select_wrap(max_fd, &fdset, NULL, NULL, &select_timeout) > 0) {
            if (listen_fd != -1 && FD_ISSET(listen_fd, &fdset)) {
                char ipstr[INET_ADDRSTRLEN];
                if ((accept_fd = accept_wrap(listen_fd, &saddr, &addr_len)) > 0) {
                    if (setsockopt(accept_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int)) != 0
                        ||
                        setsockopt(accept_fd, IPPROTO_TCP, TCP_QUICKACK, &on, sizeof(int)) != 0) {
                        log(LOGLEVEL::FATAL, "set socket option error");
                    }
                    char * ip_address = get_ip(accept_fd, ipstr, INET_ADDRSTRLEN);
                    if (_pendpool->insert_item(accept_fd) == -1) {
                        log(LOGLEVEL::FATAL, "UI connect overflow! ip=%s.sock num=%d",
                            ip_address, com_pending->_socket_num);
                        close_wrap(accept_fd);
                    } else {
                        log(LOGLEVEL::NOTICE, "accept connect from %s,queue len:%d.",
                            ip_address, _pendpool->get_queuelen());
                    }
                } else {
                    log(LOGLEVEL::WARNING, "accept request from UI error! ret=%d", accept_fd);
                    continue;
                }
            }
        }


    }
}

bool gpendingpool::start()
{
    log(LOGLEVEL::DEBUG, "start");
    if (is_exit) {
        log(LOGLEVEL::WARNING, "has STOP");
        return false;
    }
    listen_fd = -1;
    if ((listen_fd = tcplisten_wrap(get_listen_port(), get_queue_len())) < 0) {
        log(LOGLEVEL::WARNING, "fail to listen [port=%u]!", get_listen_port());
        return false;
    }
    else {
        log(LOGLEVEL::WARNING, "succ to listen port[%d] on fd[%d]", 
            get_listen_port(), listen_fd);
    }
    listen_thread = std::thread([this]{this->listen_thread_process();});
    return true;

}

bool gpendingpool::stop()
{
    log(LOGLEVEL::DEBUG, "stop");
    is_exit = true;
    this->listen_thread.join();
    return true;
}
}