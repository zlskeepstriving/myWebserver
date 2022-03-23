DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2
endif

server:main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./mysqlPool/sql_connection_pool.cpp ./threadPool/threadPool.h webserver.cpp config.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient -Wall

clean:
	rm -r server