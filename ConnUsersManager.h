#pragma once
#include <tbb/concurrent_hash_map.h>
#include <vector>
#include <mutex>

#include "ConnUser.h"

class ConnUsersManager {
public:
    ConnUsersManager(uint16_t maxClientCount_) : ConnUsers(maxClientCount_) {}
    ~ConnUsersManager() {
        for (int i = 0; i < ConnUsers.size(); i++) {
            delete ConnUsers[i];
        }
    }

    // ================== CONNECTION USER MANAGEMENT ==================
    void InsertUser(uint16_t connObjNum_, ConnUser* connUser_);
    ConnUser* FindUser(uint16_t connObjNum_);
    ConnUser* FindUserByPk(uint32_t pk_);

    void SetPkToObjNum(uint32_t pk_, uint16_t connObjNum_);
    uint16_t GetObjNumByPk(uint32_t pk_);
    void DelPkToObjNum(uint32_t pk_);


private:
    std::mutex pkMapMutex;
    std::unordered_map<uint32_t, uint16_t> pkToObjNum; // key: pk, val: connObjNum
    std::vector<ConnUser*> ConnUsers; // ConnUsers Obj
};
