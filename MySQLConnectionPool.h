#pragma once
#pragma comment (lib, "libmysql.lib")

#include "DBConfig.h"

class MySQLConnectionPool {
public:
	static MySQLConnectionPool& GetInstance();

	MYSQL* GetConnection();

	bool init();
	void Shutdown();

	MySQLConnectionPool(const MySQLConnectionPool&) = delete;
	MySQLConnectionPool& operator=(const MySQLConnectionPool&) = delete;

private:
	MySQLConnectionPool() = default;
	~MySQLConnectionPool() {
		Shutdown();
	}

	std::mutex dbPoolMutex;
	std::queue<MYSQL*> dbPool;
	std::counting_semaphore<dbConnectionCount> semaphore{ dbConnectionCount };

	int MysqlResult;
};