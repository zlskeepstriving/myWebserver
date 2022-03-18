#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool() {
    m_curConn = 0;
    m_freeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

//构造初始化
void connection_pool::init(string url, string user, string password, string databaseName, int port, int maxConn, int close_log) {
    m_url = url;
    m_user = user;
    m_port = port;
    m_databaseName = databaseName;
    m_close_log = close_log;

    for (int i = 0; i < maxConn; ++i) {
        MYSQL *con = NULL;
        con = mysql_init(con);
        if (con == nullptr) {
            perror("MYSQL Error");
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), databaseName.c_str(), port, nullptr, 0);
        if (con == nullptr) {
            perror("MYSQL ERROR");
            exit(1);
        }
        connList.push_back(con);
        ++m_freeConn;
    }

    reserve = Sem(m_freeConn);
    m_maxConn = m_freeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = nullptr;
    if (connList.size() == 0) {
        return nullptr;
    }
    
    reserve.wait();
    lock.lock();

    con = connList.front();
    connList.pop_front();

    --m_freeConn;
    ++m_curConn;

    lock.unlock();
    return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (con == nullptr) {
        return false;
    }

    lock.lock();
    
    connList.push_back(con);
    ++m_freeConn;
    --m_curConn;

    lock.unlock();

    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool() {
    lock.lock();

    if (connList.size() > 0) {
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_curConn = 0;
        m_freeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 获取当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->m_freeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

// RAII
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}