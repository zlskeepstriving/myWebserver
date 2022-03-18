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
    void adjust_timer();
    

private:

};




#endif