#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "../lock/locker.h"
#include "../mysqlPool/sql_connection_pool.h"
#include <cstdio>

template<typename T>
class ThreadPool {
public:
    ThreadPool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    bool append(T *request, int state);
    bool append_p(T *request);
    ~ThreadPool();

private:
    // 线程池中的线程数
    int m_thread_number;
    // 线程数组，大小为m_thread_number;
    pthread_t* m_threads;
    // 请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;
    //请求队列
    std::list<T*> m_work_queue;
    //互斥锁
    Locker m_queue_locker;
    //信号量用来判断是否有任务需要处理
    Sem m_queue_stat;
    // 是否结束线程
    bool m_stop;
    connection_pool *m_connPool; //数据库
    int m_actor_model; //模型切换

private:
    //为什么worker一定要是static，因为pthread_create()第三个参数为void*,普通成员函数有一个隐式的this指针而无法通过编译
    static void* worker(void *arg); 
    void run();
};

template<typename T>
ThreadPool<T>::ThreadPool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : 
    m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(NULL), m_connPool(connPool) {
        if ( (thread_number <= 0) || (max_requests <= 0)) {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }

        //创建thread_number个线程，并将它们设置为线程脱离(脱离父线程)
        for (int i = 0; i < m_thread_number; ++i) {
            printf("create the %dth thread\n", i);

            if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
                delete []m_threads;
                throw std::exception();
            }

            if (pthread_detach(m_threads[i])) {
                delete []m_threads;
                throw std::exception();
            }
        }
    }

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete []m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::append(T *request, int state) {
    m_queue_locker.lock();
    if (m_work_queue.size() > m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }

    request->m_state = state;
    m_work_queue.push_back(request);
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true;
}

template<typename T>
bool ThreadPool<T>::append_p(T* request) {
    m_queue_locker.lock();
    if (m_work_queue.size() > m_max_requests) {
        m_queue_locker.unlock();
        return false;
    }

    m_work_queue.push_back(request);
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool *pool = (ThreadPool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void ThreadPool<T>::run() {
    while (!m_stop) {
        m_queue_stat.wait();
        m_queue_locker.lock();
        if (m_work_queue.empty()) {
            m_queue_locker.unlock();
            continue;
        }

        T* request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queue_locker.unlock();
        
        if (!request) {
            continue;
        }
        if (m_actor_model == 1) {
            if (request->m_state == 0) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlconn(&request->mysql, m_connPool);
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlconn(&request->mysql, m_connPool);
            printf("55555555555555555--------\n");
            request->process();
        }
    }
}

#endif