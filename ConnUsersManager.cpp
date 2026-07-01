#include "ConnUsersManager.h"

// ================== CONNECTION USER MANAGEMENT ==================

void ConnUsersManager::InsertUser(uint16_t connObjNum_, ConnUser* connUser_) {
	ConnUsers[connObjNum_] = connUser_;
};

ConnUser* ConnUsersManager::FindUser(uint16_t connObjNum_) {
	return ConnUsers[connObjNum_];
};


void ConnUsersManager::SetPkToObjNum(uint32_t pk_, uint16_t connObjNum_) {
	std::lock_guard<std::mutex> lg{ pkMapMutex };
	pkToObjNum[pk_] = connObjNum_;
}

uint16_t ConnUsersManager::GetPkToObjNum(uint32_t pk_) {
	std::lock_guard<std::mutex> lg{ pkMapMutex };
	return pkToObjNum[pk_];
}

void ConnUsersManager::DelPkToObjNum(uint32_t pk_) {
	
}