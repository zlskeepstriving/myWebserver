#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <exception>
#include "locker.h"
#include "threadPool.h"
#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EPOLL_EVENT 10000

// 添加信号捕捉
void addSig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll中
extern void addFd(int epfd, int fd, bool one_shot);

// 从epoll中删除文件描述符
extern void removeFd(int epfd, int fd);

// 修改epoll中的文件描述符
extern void modifyFd(int epfd, int fd, int ev);

int main(int argc, char* argv[]) {

    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    //对SIGPIE信号进行处理
    addSig(SIGPIPE, SIG_IGN); //忽略一方关闭后另一方还写入导致的SIGPIE信号

    //创建线程池，初始化线程池
    ThreadPool<http_conn>* pool = NULL;
    try {
        pool = new ThreadPool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    //创建一个数组，用于保存所有的客户端信息
    http_conn* users = new http_conn[ MAX_FD ];

    int listen_fd = 0;
    if ( (listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("listen");
        exit(-1);
    }

    //设置端口复用
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (sockaddr*)&saddr, sizeof(saddr)) == -1) {
        perror("bind");
        exit(-1);
    }

    printf("Listening...\n");
    listen(listen_fd, 5);

    //创建epoll
    int epfd = epoll_create(100);
    if (epfd < 0) {
        perror("epoll_create");
        exit(-1);
    }

    //保存epoll返回的发生事件的文件描述符
    epoll_event evs[MAX_EPOLL_EVENT];

    //将监听的文件描述符加入epoll对象中
    addFd(epfd, listen_fd, false);

    //设置所有http连接的epoll fd
    http_conn::m_epollfd = epfd;

    while(true) {
        int num = epoll_wait(epfd, evs, MAX_EPOLL_EVENT, -1);
        if (num < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        //循环遍历事件数组
        for (int i = 0; i < num; ++i) {
            int sock_fd = evs[i].data.fd;
            if (sock_fd == listen_fd) {
                printf("new client request arrive\n");
                sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int conn_fd = 0;
                if ( (conn_fd = accept(sock_fd, (sockaddr*)&client_address, &client_address_len)) == -1) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD) {
                    //连接数满了，告诉客户端服务器正忙
                    close(conn_fd);
                    continue;
                }
                //将新的客户数据初始化，放入数组中
                users[conn_fd].init(conn_fd, client_address);
            } else if (evs[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或错误等事件
                printf("client closed\n");
                users[sock_fd].close_conn();
            } else if (evs[i].events & EPOLLIN) {
                if (users[sock_fd].read()) {
                    //一次性把数据都读完，然后交给工作线程
                    pool->append(users + sock_fd);
                } else {
                    users[sock_fd].close_conn();
                }
            } else if (evs[i].events & EPOLLOUT) {
                if (!users[sock_fd].write()) { //写失败了
                    users[sock_fd].close_conn();
                }
            }
        }
    }

    close(listen_fd);
    close(epfd);
    delete pool;
    delete [] users;

    return 0;
}