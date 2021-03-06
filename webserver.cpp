#include "webserver.h"

WebServer::WebServer() {
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string password, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_opt_linger = opt_linger;
    m_trig_mode = trigmode;
    m_close_log = close_log;
    m_actor_model = actor_model;
}

void WebServer::trig_mode() {
    // LT + LT
    if (m_trig_mode == 0) {
        m_listen_trig_mode = 0;
        m_conn_trig_mode = 0;
    }
    // LT + ET
    else if (m_trig_mode == 1) {
        m_listen_trig_mode = 0;
        m_conn_trig_mode = 1;
    }
    // ET + LT
    else if (m_trig_mode == 2) {
        m_listen_trig_mode = 1;
        m_conn_trig_mode = 0;
    }
    // ET + ET
    else if (m_trig_mode == 3) {
        m_listen_trig_mode = 1;
        m_conn_trig_mode = 1;
    }
}

void WebServer::sql_bool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_password, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initMysqlResult(m_connPool);
}

void WebServer::thread_pool() {
    // 线程池
    m_pool = new ThreadPool<http_conn>(m_actor_model, m_connPool, m_thread_num);
}

void WebServer::eventListen() {
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (m_opt_linger == 0) {
        linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (m_opt_linger == 1) {
        linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);

    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    ret = bind(m_listenfd, (sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_listen_trig_mode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setNonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addSig(SIGPIPE, SIG_IGN);
    utils.addSig(SIGALRM, utils.sig_handler, false);
    utils.addSig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_conn_trig_mode, m_close_log, m_user, m_password, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    printf("adjust timer once\n");
}

void WebServer::deal_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }
}

bool WebServer::dealClientData() {
    sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    if (m_listen_trig_mode == 0) {
        int connfd = accept(m_listenfd, (sockaddr*)&client_address, &client_addr_len);
        if (connfd < 0) {
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    } else {
        while(1) {
            int connfd = accept(m_listenfd, (sockaddr*)&client_address, &client_addr_len);
            if (connfd < 0) {
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                return false;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealWithSignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1 || ret == 0) {
        return false;
    } else {
        for (int i = 0; i < ret; ++i) {
            switch(signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::dealWithRead(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (m_actor_model == 1) {
        if (timer != nullptr) {
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else { 
        //proactor
        if (users[sockfd].read_once()) {
            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);
            if (timer != nullptr) {
                adjust_timer(timer);
            }
        } else {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealWithWrite(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (m_actor_model == 1) {
        if (timer != nullptr) {
            adjust_timer(timer);
        }
        m_pool->append(users + sockfd, 1);

        while (true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    } else {
        //proactor
        if (users[sockfd].write()) {
            if (timer != nullptr) {
                adjust_timer(timer);
            }
        } else {
            deal_timer(timer, sockfd);
        }
    }
}




void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            perror("epoll failure");
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            // 处理新到的客户端连接
            if (sockfd == m_listenfd) {
                bool flag = dealClientData();
                if (flag == false) {
                    continue;
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 客户端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealWithSignal(timeout, stop_server);
                if (flag == false) {
                    perror("dealclientdata failure");
                }
            }
            // 处理客户端上接收到的数据
            else if (events[i].events & EPOLLIN) {
                dealWithRead(sockfd);
            }
            // 给客户端发送响应
            else if (events[i].events & EPOLLOUT) {
                dealWithWrite(sockfd);
            }
        }
        if (timeout) {
            utils.timer_handler();
            timeout = false;
        }
    }
}