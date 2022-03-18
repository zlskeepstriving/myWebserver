#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

class util_timer;

struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 定时器类
class util_timer {
public:
    util_timer(): prev(nullptr), next(nullptr) {}

private:
    time_t expire;
    
    void (* cb_func)(client_data* ); //任务回调函数
    client_data* user_data;
    util_timer *prev;
    util_timer *next;
};


// 由定时器组成的双向链表类
class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick(); 
 
private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot); //设置超时时间

    //对文件描述符设置非阻塞
    void setNonblocking(int fd);

    //向内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int trigMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addSig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务, 重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char* info);

private:
    static int* u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *userdata);

#endif