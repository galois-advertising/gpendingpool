#include <stdarg.h>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "pendingpool.h"


namespace galois
{
unsigned int pendingpool::get_listen_port() const
{
    return 8707;
}

unsigned int pendingpool::get_queue_len() const
{
    return 5;
}

int pendingpool::get_alive_timeout_ms() const
{
    return 1024;
}
int pendingpool::get_select_timeout_ms() const
{
    return 1024;
}

void pendingpool::log(LOGLEVEL level, const char * fmt, ...) const
{
    char buf[1024];
    va_list args; 
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::cerr<<buf<<std::endl;
}

pendingpool::pendingpool() : listen_fd(-1), is_exit(false), listen_thread()
{

}

pendingpool::~pendingpool()
{

}

int pendingpool::close_wrap(int fd)
{
    return close(fd);
}

int pendingpool::listen_wrap(int sockfd, int backlog)
{
    int val = listen(sockfd, backlog);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "listen(%d,%d) call failed.error[%d] info is %s.", sockfd,
                    backlog, errno, strerror(errno));
    }
    return val;
}

int pendingpool::setsockopt_wrap(int sockfd, int level, int optname, 
    const void *optval, socklen_t optlen)
{
    int val = setsockopt(sockfd, level, optname, optval, optlen);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "setsockopt(%d,%d,%d) call failed.error[%d] info is %s.",
                    sockfd, level, optname, errno, strerror(errno));
    }
    return val;
}

int pendingpool::socket_wrap(int family, int type, int protocol)
{
    int val = socket(family, type, protocol);
    if (val == -1) {
        log(LOGLEVEL::FATAL, "socket(%d,%d,%d) call failed.error[%d] info is %s.",
                    family, type, protocol, errno, strerror(errno));
    }
    return val;
}

int pendingpool::bind_wrap(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen)
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

int pendingpool::tcplisten_wrap(int port, int queue)
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

int pendingpool::select_wrap(int nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, 
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

int pendingpool::accept_wrap(int sockfd, struct sockaddr *sa, socklen_t * addrlen)
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

void pendingpool::close_listen_fd()
{
    close_wrap(listen_fd);
    listen_fd = -1;
}

void pendingpool::listen_thread_process()
{

    fd_set fdset;
    timeval select_timeout = {
        get_select_timeout_ms() / 1000, 
        get_select_timeout_ms() * 1000 % 1000000
    };
    int max_fd = 0;
    while(!is_exit) {
        FD_ZERO(&fdset);
        if (select_wrap(max_fd, &fdset, NULL, NULL, &select_timeout) > 0) {
            if (listen_fd != -1 && FD_ISSET(listen_fd, &fdset)) {
            }
        }


    }
}

bool pendingpool::start()
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

bool pendingpool::stop()
{
    log(LOGLEVEL::DEBUG, "stop");
    is_exit = true;
    this->listen_thread.join();
    return true;
}
}