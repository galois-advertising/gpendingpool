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
	 * @brief �ر����Ӿ�� 
	 *
	 * @param [in] handle   : ��Ҫ�رյ����Ӿ��
	 * @param [in] bKeepAlive   : false�����Ӿ���رգ�true�����Ӿ�������Ϲرգ�ֻ�ǽ�socket��״̬��busyת��Ϊready״̬
	 * @return  void 
	**/
    // Close handle.bKeepAlive:[false:]
	void work_reset_item(int handle, bool bKeepAlive); 
	/**
	 * @brief ��ready״̬��socket���뵽fd_set�ļ�����������
	 *
	 * @param [out] pfs   : fd_set* �ļ��������ļ���
	 * @return  int ������sock+1
	**/
	int mask(fd_set * pfs);
	
	/**
	 * @brief ��pendingPool����һ������
	 *
	 * @param [in] sock_work   : int �ѽ������ӵ��׽���
	 * @return  int :>=0 socket������PendingPool�е�λ�� -1 ʧ��
	**/
	int insert_item(int sock_work); 
	/**
	 * @brief ���socket�����е�����socket,��socketΪbusy״̬��ֻ�Ǹ���socket������ʱ�䣬��socketΪready״̬�����socket��set,����socket����������У�����ʧ�ܻ��߳�ʱ�ر�socket
	 *
	 * @param [in] pfs   : fd_set* ����socket���ļ�������
	 * @return  void 
	**/
	void check_item(fd_set * pfs);

	/**
	 * @brief ��offsetλ�õ����Ӽ��뵽�Ѿ����Ķ�����
	 *
	 * @param [in] offset   : int ������PendingPool�е�λ��(insert_item()�ķ���ֵ)
	 * @return  int 0 ��ʾ�ɹ� -1 ��ʾʧ��
	**/
	int queue_in(int offset);
	/**
	 * @brief ���ó�ʱֵ
	 *
	 * @param [in] sec   : int Ҫ���õĳ�ʱʱ��
	 * @return  void 
	**/
	void set_timeout(int sec);	
	/**
	 * @brief �����Ѿ������еĳ��ȣ�ע�⣺�˺�����set_socknum��������������֮ǰ���е��ã��������̷߳���poolǰ���ò��Ҳ��ܶ�̬����
	 *
	 * @param [in] len   : int �����õĳ���
	 * @return  void 
	**/
	void set_queuelen(int len);	
	/**
	 * @brief ���ÿɴ洢socket��������ע�⣺�˺�����set_queuelen��������������֮ǰ���е��ã��������̷߳���poolǰ���ò��Ҳ��ܶ�̬������
	 *
	 * @param [in] num   : int �����õ�����
	 * @return  void 
	**/
	void set_socknum(int num);	
	/**
	 * @brief ��ȡ�ȴ���ȡ���ӵ��߳���
	 *
	 * @return  int �ȴ���ȡ���ӵ��߳���
	**/
	int get_freethread();	
	/**
	 * @brief ��ȡ�Ѿ������еĳ��ȣ����صĳ��ȱ�set_queuelen()���õĳ�����һ��
	 *
	 * @return  int �Ѿ������еĳ���
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