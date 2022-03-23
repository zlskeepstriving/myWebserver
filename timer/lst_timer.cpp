#include "lst_timer.h"
#include "../http/http_conn.h"


// 以下为sort_timer_lst类成员函数定义

sort_timer_lst::sort_timer_lst() {
    head = nullptr;
    tail = nullptr;
}

//回收所有定时器节点的资源
sort_timer_lst::~sort_timer_lst() {
    util_timer *tmp = head;
    while (tmp != nullptr) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer) {
    if (timer == nullptr) {
        return;
    }
    if (head == nullptr) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    // find pos for insert timer
    while (tmp != nullptr) {
        if (timer->expire < tmp->expire) { //found
            prev->next = timer;
            timer->prev = prev;
            timer->next = tmp;
            tmp->prev = timer;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // no correct insert pos, insert to last
    if (tmp == nullptr) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (timer == nullptr) {
        return;
    }
    util_timer *tmp = timer->next;
    if (tmp == nullptr || timer->expire < tmp->expire) {
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    } else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        //从下个定时器开始，找到其合适的插入位置
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer* timer) {
    if (timer == nullptr) {
        return;
    }
    if (timer == head && timer == tail) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
} 

//触发定时器链表上的所有已超时任务
void sort_timer_lst::tick() {
    if (head == nullptr) {
        return;
    }
    time_t cur = time(NULL);
    util_timer* tmp = head;
    while (tmp != nullptr) {
        if (tmp->expire > cur) {
            break;
        }
        tmp->cb_func(tmp->user_data); //触发回调函数,删除该连接
        head = tmp->next;
        if (head != nullptr) {
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}

// 以下为Utils类成员函数和static成员定义

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

void Utils::setNonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | SOCK_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int trigMode) {
    epoll_event ev;
    ev.data.fd = fd;
    if (trigMode == 1) {
        ev.events = EPOLLIN | EPOLLET || EPOLLRDHUP;
    } else {
        ev.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        ev.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setNonblocking(fd);
}

void Utils::sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addSig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

class Utils;
void cb_func(client_data* user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    --http_conn::m_user_count;
}

