#pragma once
#pragma comment (lib, "libmysql.lib")

#include <optional>

#include "DBConfig.h"
#include "UserTypes.h"
#include "FailCode.h"

class MySQLManager {
public:
	static MySQLManager& GetInstance();

	MYSQL* GetConnection();

	bool init();
	void Shutdown();

	std::optional<UserSessionInfo> GetUserSessionInfo(uint32_t userPk_);


	std::optional<uint32_t> AcceptFriend(uint32_t userPk_, const std::string& targetId_);
	std::optional<uint32_t> RemoveFriend(uint32_t userPk_, const std::string& targetId_);
	std::optional<std::vector<uint32_t>> GetUserFriendsPks(uint32_t userPk_);
	std::optional<UserSearchResult> SearchUser(const std::string& userId_);
	std::optional<uint32_t> SendFriendRequest(uint32_t userPk_, const std::string& targetId_);


	std::optional<CostumeChangeFailCode> UpdateEquipSlot(uint32_t userPk_, uint8_t slot_, uint32_t itemCode_);



	MySQLManager(const MySQLManager&) = delete;
	MySQLManager& operator=(const MySQLManager&) = delete;

private:
	MySQLManager() = default;
	~MySQLManager() {
		Shutdown();
	}

	std::mutex dbPoolMutex;
	std::queue<MYSQL*> dbPool;
	std::counting_semaphore<dbConnectionCount> semaphore{ dbConnectionCount };

	int MysqlResult;
};