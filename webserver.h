#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <cstring>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadPool/threadPool.h"
#include "./http/http_conn.h"
#include "./mysqlPool/sql_connection_pool.h"
#include "./timer/lst_timer.h"

const int MAX_FD = 65536; //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5; //最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init (int port, std::string user, std::string password, std::string databaseName, 
                int log_write, int opt_linger, int trigmode, int sql_num,
                int thread_num, int close_log, int actor_model);
    void thread_pool();
    void sql_bool();
    void log_write();
    void trig_mode();
    void eventListen(); //监听
    void eventLoop(); //主循环
    void timer(int connfd, sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool &timeout, bool& stop_server);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

private:
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;
    int m_actor_model;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    // 数据库相关
    connection_pool* m_connPool;
    string m_user;
    string m_password;
    string m_databaseName;
    int m_sql_num;

    // 线程池相关
    ThreadPool<http_conn> *m_pool;
    int m_thread_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_opt_linger;
    int m_trig_mode;
    int m_listen_trig_mode;
    int m_conn_trig_mode;

    // 定时器相关
    client_data *users_timer;
    Utils utils;
};




#endif