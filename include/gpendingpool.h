#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>

namespace galois
{

enum class LOGLEVEL {FATAL, WARNING, NOTICE, DEBUG};

class pendingpool
{
public: 
    pendingpool();
    ~pendingpool();
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
    // Get a READY socket and mark it to BUSY. (out,out,out)
	bool work_fetch_item(int &handle, int &sock, int &wait);

	/**
	 * @brief 关闭连接句柄 
	 *
	 * @param [in] handle   : 需要关闭的连接句柄
	 * @param [in] bKeepAlive   : false短连接句柄关闭，true长连接句柄不马上关闭，只是将socket的状态由busy转换为ready状态
	 * @return  void 
	**/
    // Close handle.bKeepAlive:[false:]
	void work_reset_item(int handle, bool bKeepAlive); 
	/**
	 * @brief 将ready状态的socket加入到fd_set文件描述符集合
	 *
	 * @param [out] pfs   : fd_set* 文件描述符的集合
	 * @return  int ：最大的sock+1
	**/
	int mask(fd_set * pfs);
	
	/**
	 * @brief 向pendingPool加入一个连接
	 *
	 * @param [in] sock_work   : int 已建立连接的套节字
	 * @return  int :>=0 socket连接在PendingPool中的位置 -1 失败
	**/
	int insert_item(int sock_work); 
	/**
	 * @brief 检查socket队列中的所有socket,当socket为busy状态，只是更新socket的最近活动时间，当socket为ready状态，如果socket被set,将此socket加入就绪队列，加入失败或者超时关闭socket
	 *
	 * @param [in] pfs   : fd_set* 所有socket的文件描述符
	 * @return  void 
	**/
	void check_item(fd_set * pfs);

	/**
	 * @brief 将offset位置的连接加入到已就绪的队列中
	 *
	 * @param [in] offset   : int 连接在PendingPool中的位置(insert_item()的返回值)
	 * @return  int 0 表示成功 -1 表示失败
	**/
	int queue_in(int offset);
	/**
	 * @brief 设置超时值
	 *
	 * @param [in] sec   : int 要设置的超时时间
	 * @return  void 
	**/
	void set_timeout(int sec);	
	/**
	 * @brief 设置已就绪队列的长度，注意：此函数和set_socknum必须在其他函数之前进行调用，必须在线程访问pool前设置并且不能动态调整
	 *
	 * @param [in] len   : int 待设置的长度
	 * @return  void 
	**/
	void set_queuelen(int len);	
	/**
	 * @brief 设置可存储socket的数量，注意：此函数和set_queuelen必须在其他函数之前进行调用，必须在线程访问pool前设置并且不能动态调整的
	 *
	 * @param [in] num   : int 待设置的数量
	 * @return  void 
	**/
	void set_socknum(int num);	
	/**
	 * @brief 获取等待获取连接的线程数
	 *
	 * @return  int 等待获取连接的线程数
	**/
	int get_freethread();	
	/**
	 * @brief 获取已就绪队列的长度，返回的长度比set_queuelen()设置的长度少一个
	 *
	 * @return  int 已就绪队列的长度
	**/
	int get_queuelen();
private:
    void listen_thread_process();
    const char * get_ip(int fd, char* ipstr, size_t len);
    int listen_fd;
    std::atomic_bool is_exit;
    std::thread listen_thread; 
};

}