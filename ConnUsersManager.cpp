#include "ConnUsersManager.h"

// ================== CONNECTION USER MANAGEMENT ==================

void ConnUsersManager::InsertUser(uint16_t connObjNum_, ConnUser* connUser_) {
	ConnUsers[connObjNum_] = connUser_;
};

ConnUser* ConnUsersManager::FindUser(uint16_t connObjNum_) {
	return ConnUsers[connObjNum_];
};

ConnUser* ConnUsersManager::FindUserByPk(uint32_t pk_) {
	return ConnUsers[GetObjNumByPk(pk_)];
}

void ConnUsersManager::SetPkToObjNum(uint32_t pk_, uint16_t connObjNum_) {
	std::lock_guard<std::mutex> lg{ pkMapMutex };
	pkToObjNum[pk_] = connObjNum_;
}

uint16_t ConnUsersManager::GetObjNumByPk(uint32_t pk_) {
	std::lock_guard<std::mutex> lg{ pkMapMutex };
	return pkToObjNum[pk_];
}

void ConnUsersManager::DelPkToObjNum(uint32_t pk_) {
	std::lock_guard<std::mutex> lg{ pkMapMutex };
	pkToObjNum.erase(pk_);
}