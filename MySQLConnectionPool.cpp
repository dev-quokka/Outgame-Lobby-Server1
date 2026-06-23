#include "MySQLConnectionPool.h"

MySQLConnectionPool& MySQLConnectionPool::GetInstance() {
    static MySQLConnectionPool instance;
    return instance;
}

MYSQL* MySQLConnectionPool::GetConnection() {
    MYSQL* ConnPtr;
    {
        std::lock_guard<std::mutex> lock(dbPoolMutex);
        if (dbPool.empty()) {
            return nullptr;
        }
        ConnPtr = dbPool.front();
        dbPool.pop();
    }
    return ConnPtr;
};

bool MySQLConnectionPool::init() {
    for (int i = 0; i < dbConnectionCount; i++) {
        MYSQL* Conn = mysql_init(nullptr);
        if (!Conn) {
            std::cerr << "Mysql Init Fail" << std::endl;
            return false;
        }

        MYSQL* ConnPtr = mysql_real_connect(Conn, DB_HOST, DB_USER, DB_PASSWORD, DB_NAME, DB_PORT, (char*)NULL, 0);
        if (!ConnPtr) {
            std::cerr << "Mysql Connection Fail : " << mysql_error(Conn) << '\n';
            return false;
        }

        dbPool.push(ConnPtr);
    }

    std::cout << "Mysql Connection Success" << std::endl;
    return true;
}

void MySQLConnectionPool::Shutdown() {
    while (!dbPool.empty()) {
        MYSQL* conn = dbPool.front();
        dbPool.pop();
        mysql_close(conn);
    }
}

//bool MySQLManager::GetEquipmentItemData(std::unordered_map<ItemDataKey, std::unique_ptr<ItemData>, ItemDataKeyHash>& itemData_) {
//    semaphore.acquire();
//
//    MYSQL* ConnPtr = GetConnection();
//    if (!ConnPtr) {
//        std::cerr << "[GetEquipmentItemData] dbPool is empty. Failed to get DB connection." << '\n';
//        return false;
//    }
//
//    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);
//
//    MYSQL_RES* Result;
//    MYSQL_ROW Row;
//
//    std::string query_s = "SELECT item_code, itemName, attackPower FROM EquipmentData";
//    const char* Query = query_s.c_str();
//
//    if (mysql_query(ConnPtr, Query) != 0) {
//        std::cerr << "[GetEquipmentItemData] Query Failed : " << mysql_error(ConnPtr) << std::endl;
//        return false;
//    }
//
//    try {
//        Result = mysql_store_result(ConnPtr);
//        if (Result == nullptr) {
//            std::cerr << "[GetEquipmentItemData] Failed to store result : " << mysql_error(ConnPtr) << std::endl;
//            return false;
//        }
//
//        while ((Row = mysql_fetch_row(Result)) != NULL) {
//            if (!Row[0] || !Row[1] || !Row[2]) continue;
//
//            auto equipmentData = std::make_unique<EquipmentItemData>();
//            equipmentData->itemCode = (uint16_t)std::stoi(Row[0]);
//            equipmentData->itemName = Row[1];
//            equipmentData->attackPower = (uint16_t)std::stoi(Row[2]);
//            equipmentData->itemType = ItemType::EQUIPMENT;
//
//            itemData_[{equipmentData->itemCode, static_cast<uint16_t>(equipmentData->itemType)}] = std::move(equipmentData);
//        }
//
//        mysql_free_result(Result);
//
//        return true;
//    }
//    catch (const std::exception& e) {
//        std::cerr << "[GetEquipmentItemData] Exception Error : " << e.what() << std::endl;
//        return false;
//    }
//}