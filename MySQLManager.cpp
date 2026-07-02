#include "MySQLManager.h"

MySQLManager& MySQLManager::GetInstance() {
    static MySQLManager instance;
    return instance;
}

MYSQL* MySQLManager::GetConnection() {
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

bool MySQLManager::init() {
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

void MySQLManager::Shutdown() {
    while (!dbPool.empty()) {
        MYSQL* conn = dbPool.front();
        dbPool.pop();
        mysql_close(conn);
    }
}
\
std::optional<UserSessionInfo> MySQLManager::GetUserSessionInfo(uint32_t userPk_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[GetUserSessionInfo] dbPool is empty.\n";
        return std::nullopt;
    }

    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        MYSQL_STMT* stmt = mysql_stmt_init(ConnPtr);
        std::string query =
            "SELECT user_level, user_exp "
            "FROM user WHERE user_pk = ?";

        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
            std::cerr << "[GetUserSessionInfo] Prepare Error: " << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        MYSQL_BIND param[1];
        memset(param, 0, sizeof(param));
        param[0].buffer_type = MYSQL_TYPE_LONG;
        param[0].buffer = &userPk_;
        param[0].is_unsigned = true;
        mysql_stmt_bind_param(stmt, param);

        if (mysql_stmt_execute(stmt) != 0) {
            std::cerr << "[GetUserSessionInfo] Execute Error: " << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        UserSessionInfo info;
        MYSQL_BIND result[2];
        memset(result, 0, sizeof(result));

        result[0].buffer_type = MYSQL_TYPE_SHORT;
        result[0].buffer = &info.userLevel;
        result[0].is_unsigned = true;

        result[1].buffer_type = MYSQL_TYPE_LONG;
        result[1].buffer = &info.userExp;
        result[1].is_unsigned = true;

        mysql_stmt_bind_result(stmt, result);
        mysql_stmt_store_result(stmt);

        if (mysql_stmt_fetch(stmt) != 0) {
            std::cerr << "[GetUserSessionInfo] User not found. userPk: " << userPk_ << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        mysql_stmt_close(stmt);
        return info;
    }
    catch (const std::exception& e) {
        std::cerr << "[GetUserSessionInfo] Exception: " << e.what() << '\n';
        return std::nullopt;
    }
}

std::optional<uint32_t> MySQLManager::AcceptFriend(uint32_t userPk_, const std::string& targetId_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[AcceptFriend] dbPool is empty.\n";
        return std::nullopt;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        if (mysql_query(ConnPtr, "START TRANSACTION") != 0) {
            std::cerr << "[AcceptFriend] Transaction start failed.\n";
            return std::nullopt;
        }

        // 1. targetId → targetPk 조회
        uint32_t targetPk = 0;
        MYSQL_STMT* stmtSelect = mysql_stmt_init(ConnPtr);
        std::string selectQuery =
            "SELECT user_pk FROM user WHERE user_id = ?";

        if (mysql_stmt_prepare(stmtSelect,
            selectQuery.c_str(), selectQuery.length()) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }

        MYSQL_BIND selectParam[1];
        memset(selectParam, 0, sizeof(selectParam));
        unsigned long idLen = targetId_.length();
        selectParam[0].buffer_type = MYSQL_TYPE_STRING;
        selectParam[0].buffer = (void*)targetId_.c_str();
        selectParam[0].buffer_length = targetId_.length();
        selectParam[0].length = &idLen;
        mysql_stmt_bind_param(stmtSelect, selectParam);

        MYSQL_BIND selectResult[1];
        memset(selectResult, 0, sizeof(selectResult));
        selectResult[0].buffer_type = MYSQL_TYPE_LONG;
        selectResult[0].buffer = &targetPk;
        selectResult[0].is_unsigned = true;
        mysql_stmt_bind_result(stmtSelect, selectResult);
        mysql_stmt_execute(stmtSelect);
        mysql_stmt_store_result(stmtSelect);

        if (mysql_stmt_fetch(stmtSelect) != 0) {
            std::cerr << "[AcceptFriend] Target not found: "
                << targetId_ << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }
        mysql_stmt_close(stmtSelect);

        // 2. 양방향 UPDATE status=1
        MYSQL_STMT* stmtUpdate = mysql_stmt_init(ConnPtr);
        std::string updateQuery =
            "UPDATE friend SET status = 1 "
            "WHERE (user_pk = ? AND friend_pk = ?) "
            "   OR (user_pk = ? AND friend_pk = ?)";

        if (mysql_stmt_prepare(stmtUpdate,
            updateQuery.c_str(), updateQuery.length()) != 0) {
            std::cerr << "[AcceptFriend] Prepare Error: "
                << mysql_stmt_error(stmtUpdate) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return std::nullopt;
        }

        MYSQL_BIND param[4];
        memset(param, 0, sizeof(param));
        // (userPk, targetPk) OR (targetPk, userPk)
        param[0].buffer_type = MYSQL_TYPE_LONG;
        param[0].buffer = &userPk_;
        param[0].is_unsigned = true;

        param[1].buffer_type = MYSQL_TYPE_LONG;
        param[1].buffer = &targetPk;
        param[1].is_unsigned = true;

        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &targetPk;
        param[2].is_unsigned = true;

        param[3].buffer_type = MYSQL_TYPE_LONG;
        param[3].buffer = &userPk_;
        param[3].is_unsigned = true;

        if (mysql_stmt_bind_param(stmtUpdate, param) != 0) {
            std::cerr << "[AcceptFriend] Bind Error: "
                << mysql_stmt_error(stmtUpdate) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return std::nullopt;
        }

        if (mysql_stmt_execute(stmtUpdate) != 0) {
            std::cerr << "[AcceptFriend] Execute Error: "
                << mysql_stmt_error(stmtUpdate) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return std::nullopt;
        }

        if (mysql_stmt_affected_rows(stmtUpdate) != 2) {
            std::cerr << "[AcceptFriend] Unexpected affected rows.\n";
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return std::nullopt;
        }

        mysql_stmt_close(stmtUpdate);
        mysql_query(ConnPtr, "COMMIT");

        std::cout << "[AcceptFriend] Success. userPk: " << userPk_
            << " targetPk: " << targetPk << '\n';

        return targetPk;  // pk 반환
    }
    catch (const std::exception& e) {
        std::cerr << "[AcceptFriend] Exception: " << e.what() << '\n';
        mysql_query(ConnPtr, "ROLLBACK");
        return std::nullopt;
    }
}

std::optional<uint32_t> MySQLManager::RemoveFriend(uint32_t userPk_, const std::string& targetId_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[RemoveFriend] dbPool is empty.\n";
        return std::nullopt;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        if (mysql_query(ConnPtr, "START TRANSACTION") != 0) {
            std::cerr << "[RemoveFriend] Transaction start failed.\n";
            return std::nullopt;
        }

        // 1. targetId로 targetPk 조회
        uint32_t targetPk = 0;
        MYSQL_STMT* stmtSelect = mysql_stmt_init(ConnPtr);
        std::string selectQuery = "SELECT user_pk FROM user WHERE user_id = ?";

        if (mysql_stmt_prepare(stmtSelect,
            selectQuery.c_str(), selectQuery.length()) != 0) {
            std::cerr << "[RemoveFriend] Select Prepare Error: " << mysql_stmt_error(stmtSelect) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }

        MYSQL_BIND selectParam[1];
        memset(selectParam, 0, sizeof(selectParam));
        unsigned long idLen = targetId_.length();
        selectParam[0].buffer_type = MYSQL_TYPE_STRING;
        selectParam[0].buffer = (void*)targetId_.c_str();
        selectParam[0].buffer_length = targetId_.length();
        selectParam[0].length = &idLen;
        mysql_stmt_bind_param(stmtSelect, selectParam);

        MYSQL_BIND selectResult[1];
        memset(selectResult, 0, sizeof(selectResult));
        selectResult[0].buffer_type = MYSQL_TYPE_LONG;
        selectResult[0].buffer = &targetPk;
        selectResult[0].is_unsigned = true;
        mysql_stmt_bind_result(stmtSelect, selectResult);
        mysql_stmt_execute(stmtSelect);
        mysql_stmt_store_result(stmtSelect);

        if (mysql_stmt_fetch(stmtSelect) != 0) {
            std::cerr << "[RemoveFriend] Target not found: " << targetId_ << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }
        mysql_stmt_close(stmtSelect);

        // 2. 양방향 DELETE
        MYSQL_STMT* stmtDelete = mysql_stmt_init(ConnPtr);
        std::string deleteQuery =
            "DELETE FROM friend "
            "WHERE (user_pk = ? AND friend_pk = ?) "
            "   OR (user_pk = ? AND friend_pk = ?)";

        if (mysql_stmt_prepare(stmtDelete,
            deleteQuery.c_str(), deleteQuery.length()) != 0) {
            std::cerr << "[RemoveFriend] Delete Prepare Error: " << mysql_stmt_error(stmtDelete) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtDelete);
            return std::nullopt;
        }

        MYSQL_BIND param[4];
        memset(param, 0, sizeof(param));

        param[0].buffer_type = MYSQL_TYPE_LONG;
        param[0].buffer = &userPk_;
        param[0].is_unsigned = true;

        param[1].buffer_type = MYSQL_TYPE_LONG;
        param[1].buffer = &targetPk;
        param[1].is_unsigned = true;

        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &targetPk;
        param[2].is_unsigned = true;

        param[3].buffer_type = MYSQL_TYPE_LONG;
        param[3].buffer = &userPk_;
        param[3].is_unsigned = true;

        if (mysql_stmt_bind_param(stmtDelete, param) != 0) {
            std::cerr << "[RemoveFriend] Bind Error: " << mysql_stmt_error(stmtDelete) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtDelete);
            return std::nullopt;
        }

        if (mysql_stmt_execute(stmtDelete) != 0) {
            std::cerr << "[RemoveFriend] Execute Error: " << mysql_stmt_error(stmtDelete) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtDelete);
            return std::nullopt;
        }

        // affected_rows 확인
        // 친구 삭제 2개 (양방향)
        // 요청 취소/거절 2개 (양방향)
        auto affectedRows = mysql_stmt_affected_rows(stmtDelete);
        if (affectedRows == 0) {
            std::cerr << "[RemoveFriend] No rows deleted. " << "userPk: " << userPk_ << " targetId: " << targetId_ << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtDelete);
            return std::nullopt;
        }

        mysql_stmt_close(stmtDelete);
        mysql_query(ConnPtr, "COMMIT");

        std::cout << "[RemoveFriend] Success. userPk: " << userPk_ << " targetPk: " << targetPk << " rows: " << affectedRows << '\n';
        return targetPk;
    }
    catch (const std::exception& e) {
        std::cerr << "[RemoveFriend] Exception: " << e.what() << '\n';
        mysql_query(ConnPtr, "ROLLBACK");
        return std::nullopt;
    }
}

std::optional<std::vector<uint32_t>> MySQLManager::GetUserFriendsPks(uint32_t userPk_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[GetUserFriendsPks] dbPool is empty.\n";
        return std::nullopt;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        MYSQL_STMT* stmt = mysql_stmt_init(ConnPtr);

        std::string query =
            "SELECT f.friend_pk "
            "FROM friend f "
            "WHERE f.user_pk = ?";

        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
            std::cerr << "[GetUserFriendsPks] Prepare Error: " << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        // 입력 바인딩
        MYSQL_BIND param[1];
        memset(param, 0, sizeof(param));
        param[0].buffer_type = MYSQL_TYPE_LONG;
        param[0].buffer = &userPk_;
        param[0].is_unsigned = true;
        mysql_stmt_bind_param(stmt, param);

        if (mysql_stmt_execute(stmt) != 0) {
            std::cerr << "[GetUserFriendsPks] Execute Error: " << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        // 결과 바인딩
        uint32_t friendPk = 0;

        MYSQL_BIND result[1];
        memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_LONG;
        result[0].buffer = &friendPk;
        result[0].is_unsigned = true;

        mysql_stmt_bind_result(stmt, result);
        mysql_stmt_store_result(stmt);

        std::vector<uint32_t> friendPks;
        while (mysql_stmt_fetch(stmt) == 0) {
            friendPks.push_back(friendPk);
        }

        mysql_stmt_close(stmt);
        return friendPks;
    }
    catch (const std::exception& e) {
        std::cerr << "[GetUserFriendsPks] Exception: " << e.what() << '\n';
        return std::nullopt;
    }
}

std::optional<UserSearchResult> MySQLManager::SearchUser(const std::string& userId_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[SearchUser] dbPool is empty.\n";
        return std::nullopt;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        MYSQL_STMT* stmt = mysql_stmt_init(ConnPtr);
        std::string query =
            "SELECT user_pk, user_id, user_level "
            "FROM user WHERE user_id = ?";

        if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
            std::cerr << "[SearchUser] Prepare Error: " << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        MYSQL_BIND param[1];
        memset(param, 0, sizeof(param));
        unsigned long idLen = userId_.length();
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = (void*)userId_.c_str();
        param[0].buffer_length = userId_.length();
        param[0].length = &idLen;
        mysql_stmt_bind_param(stmt, param);

        if (mysql_stmt_execute(stmt) != 0) {
            std::cerr << "[SearchUser] Execute Error: "
                << mysql_stmt_error(stmt) << '\n';
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        UserSearchResult result;
        char     userId[MAX_USER_ID_LEN] = {};
        uint16_t userLevel = 0;
        unsigned long userIdLen = 0;

        MYSQL_BIND res[3];
        memset(res, 0, sizeof(res));

        res[0].buffer_type = MYSQL_TYPE_LONG;
        res[0].buffer = &result.userPk;
        res[0].is_unsigned = true;

        res[1].buffer_type = MYSQL_TYPE_STRING;
        res[1].buffer = userId;
        res[1].buffer_length = sizeof(userId);
        res[1].length = &userIdLen;

        res[2].buffer_type = MYSQL_TYPE_SHORT;
        res[2].buffer = &userLevel;
        res[2].is_unsigned = true;

        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);

        if (mysql_stmt_fetch(stmt) != 0) { // 유저 없음
            mysql_stmt_close(stmt);
            return std::nullopt;
        }

        strncpy_s(result.userId, sizeof(result.userId),
            userId, userIdLen);
        result.userLevel = userLevel;

        mysql_stmt_close(stmt);
        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "[SearchUser] Exception: " << e.what() << '\n';
        return std::nullopt;
    }
}

std::optional<uint32_t> MySQLManager::SendFriendRequest(uint32_t userPk_, const std::string& targetId_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        return std::nullopt;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        if (mysql_query(ConnPtr, "START TRANSACTION") != 0) {
            return std::nullopt;
        }

        // 1. targetId로 targetPk 조회
        uint32_t targetPk = 0;
        MYSQL_STMT* stmtSelect = mysql_stmt_init(ConnPtr);
        std::string selectQuery =
            "SELECT user_pk FROM user WHERE user_id = ?";

        if (mysql_stmt_prepare(stmtSelect,
            selectQuery.c_str(), selectQuery.length()) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }

        MYSQL_BIND selectParam[1];
        memset(selectParam, 0, sizeof(selectParam));
        unsigned long idLen = targetId_.length();
        selectParam[0].buffer_type = MYSQL_TYPE_STRING;
        selectParam[0].buffer = (void*)targetId_.c_str();
        selectParam[0].buffer_length = targetId_.length();
        selectParam[0].length = &idLen;
        mysql_stmt_bind_param(stmtSelect, selectParam);

        MYSQL_BIND selectResult[1];
        memset(selectResult, 0, sizeof(selectResult));
        selectResult[0].buffer_type = MYSQL_TYPE_LONG;
        selectResult[0].buffer = &targetPk;
        selectResult[0].is_unsigned = true;
        mysql_stmt_bind_result(stmtSelect, selectResult);
        mysql_stmt_execute(stmtSelect);
        mysql_stmt_store_result(stmtSelect);

        if (mysql_stmt_fetch(stmtSelect) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtSelect);
            return std::nullopt;
        }
        mysql_stmt_close(stmtSelect);

        // 2. 이미 친구거나 요청중인지 확인
        MYSQL_STMT* stmtCheck = mysql_stmt_init(ConnPtr);
        std::string checkQuery =
            "SELECT status FROM friend "
            "WHERE user_pk = ? AND friend_pk = ?";

        if (mysql_stmt_prepare(stmtCheck,
            checkQuery.c_str(), checkQuery.length()) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtCheck);
            return std::nullopt;
        }

        MYSQL_BIND checkParam[2];
        memset(checkParam, 0, sizeof(checkParam));
        checkParam[0].buffer_type = MYSQL_TYPE_LONG;
        checkParam[0].buffer = &userPk_;
        checkParam[0].is_unsigned = true;
        checkParam[1].buffer_type = MYSQL_TYPE_LONG;
        checkParam[1].buffer = &targetPk;
        checkParam[1].is_unsigned = true;
        mysql_stmt_bind_param(stmtCheck, checkParam);

        uint8_t existStatus = 255;
        MYSQL_BIND checkResult[1];
        memset(checkResult, 0, sizeof(checkResult));
        checkResult[0].buffer_type = MYSQL_TYPE_TINY;
        checkResult[0].buffer = &existStatus;
        checkResult[0].is_unsigned = true;
        mysql_stmt_bind_result(stmtCheck, checkResult);
        mysql_stmt_execute(stmtCheck);
        mysql_stmt_store_result(stmtCheck);

        if (mysql_stmt_fetch(stmtCheck) == 0) { // 이미 관계가 있음
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtCheck);
            return std::nullopt;
        }
        mysql_stmt_close(stmtCheck);

        // 3. 양방향 INSERT
        // (A에서 B, status=0 내가 보낸 요청)
        // (B에서 A, status=2 받은 요청)
        MYSQL_STMT* stmtInsert = mysql_stmt_init(ConnPtr);
        std::string insertQuery =
            "INSERT INTO friend (user_pk, friend_pk, status) VALUES "
            "(?, ?, 0), (?, ?, 2)";

        if (mysql_stmt_prepare(stmtInsert,
            insertQuery.c_str(), insertQuery.length()) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtInsert);
            return std::nullopt;
        }

        MYSQL_BIND insertParam[4];
        memset(insertParam, 0, sizeof(insertParam));

        insertParam[0].buffer_type = MYSQL_TYPE_LONG;
        insertParam[0].buffer = &userPk_;
        insertParam[0].is_unsigned = true;

        insertParam[1].buffer_type = MYSQL_TYPE_LONG;
        insertParam[1].buffer = &targetPk;
        insertParam[1].is_unsigned = true;

        insertParam[2].buffer_type = MYSQL_TYPE_LONG;
        insertParam[2].buffer = &targetPk;
        insertParam[2].is_unsigned = true;

        insertParam[3].buffer_type = MYSQL_TYPE_LONG;
        insertParam[3].buffer = &userPk_;
        insertParam[3].is_unsigned = true;

        if (mysql_stmt_bind_param(stmtInsert, insertParam) != 0 ||
            mysql_stmt_execute(stmtInsert) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtInsert);
            return std::nullopt;
        }

        mysql_stmt_close(stmtInsert);
        mysql_query(ConnPtr, "COMMIT");

        std::cout << "[SendFriendRequest] Success. userPk: " 
            << userPk_ << " targetPk: " << targetPk << '\n';
        return targetPk;
    }
    catch (const std::exception& e) {
        std::cerr << "[SendFriendRequest] Exception: " << e.what() << '\n';
        mysql_query(ConnPtr, "ROLLBACK");
        return std::nullopt;
    }
}



std::optional<CostumeChangeFailCode> MySQLManager::UpdateEquipSlot(uint32_t userPk_, uint8_t slot_, uint32_t itemCode_) {
    semaphore.acquire();

    MYSQL* ConnPtr = GetConnection();
    if (!ConnPtr) {
        std::cerr << "[UpdateEquipSlot] dbPool is empty.\n";
        return CostumeChangeFailCode::ServerError;
    }
    auto tempAutoConn = AutoConn(ConnPtr, dbPool, dbPoolMutex, semaphore);

    try {
        if (mysql_query(ConnPtr, "START TRANSACTION") != 0) {
            return CostumeChangeFailCode::ServerError;
        }

        // 1. 슬롯 유효성 확인
        if (slot_ < 1 || slot_ > 4) {
            mysql_query(ConnPtr, "ROLLBACK");
            return CostumeChangeFailCode::InvalidSlot;
        }

        // 2. 인벤토리에 해당 아이템 있는지 확인
        MYSQL_STMT* stmtCheck = mysql_stmt_init(ConnPtr);
        std::string checkQuery =
            "SELECT COUNT(*) FROM user_inventory "
            "WHERE user_pk = ? AND item_code = ? AND item_type = 1";
        // item_type=1 이 코스튬

        if (mysql_stmt_prepare(stmtCheck,
            checkQuery.c_str(), checkQuery.length()) != 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtCheck);
            return CostumeChangeFailCode::ServerError;
        }

        MYSQL_BIND checkParam[2];
        memset(checkParam, 0, sizeof(checkParam));
        checkParam[0].buffer_type = MYSQL_TYPE_LONG;
        checkParam[0].buffer = &userPk_;
        checkParam[0].is_unsigned = true;
        checkParam[1].buffer_type = MYSQL_TYPE_LONG;
        checkParam[1].buffer = &itemCode_;
        checkParam[1].is_unsigned = true;
        mysql_stmt_bind_param(stmtCheck, checkParam);

        uint32_t count = 0;
        MYSQL_BIND checkResult[1];
        memset(checkResult, 0, sizeof(checkResult));
        checkResult[0].buffer_type = MYSQL_TYPE_LONG;
        checkResult[0].buffer = &count;
        checkResult[0].is_unsigned = true;
        mysql_stmt_bind_result(stmtCheck, checkResult);
        mysql_stmt_execute(stmtCheck);
        mysql_stmt_store_result(stmtCheck);
        mysql_stmt_fetch(stmtCheck);
        mysql_stmt_close(stmtCheck);

        if (count == 0) {
            mysql_query(ConnPtr, "ROLLBACK");
            return CostumeChangeFailCode::NotInInventory;
        }

        // 3. user_equip_slot UPDATE
        MYSQL_STMT* stmtUpdate = mysql_stmt_init(ConnPtr);
        std::string updateQuery =
            "INSERT INTO user_equip_slot (user_pk, slot_type, item_code) "
            "VALUES (?, ?, ?) "
            "ON DUPLICATE KEY UPDATE item_code = ?";

        if (mysql_stmt_prepare(stmtUpdate,
            updateQuery.c_str(), updateQuery.length()) != 0) {
            std::cerr << "[UpdateEquipSlot] Prepare Error: " << mysql_stmt_error(stmtUpdate) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return CostumeChangeFailCode::ServerError;
        }

        MYSQL_BIND updateParam[4];
        memset(updateParam, 0, sizeof(updateParam));

        updateParam[0].buffer_type = MYSQL_TYPE_LONG;
        updateParam[0].buffer = &userPk_;
        updateParam[0].is_unsigned = true;

        updateParam[1].buffer_type = MYSQL_TYPE_TINY;
        updateParam[1].buffer = &slot_;
        updateParam[1].is_unsigned = true;

        updateParam[2].buffer_type = MYSQL_TYPE_LONG;
        updateParam[2].buffer = &itemCode_;
        updateParam[2].is_unsigned = true;

        updateParam[3].buffer_type = MYSQL_TYPE_LONG;
        updateParam[3].buffer = &itemCode_;
        updateParam[3].is_unsigned = true;

        if (mysql_stmt_bind_param(stmtUpdate, updateParam) != 0 ||
            mysql_stmt_execute(stmtUpdate) != 0) {
            std::cerr << "[UpdateEquipSlot] Execute Error: " << mysql_stmt_error(stmtUpdate) << '\n';
            mysql_query(ConnPtr, "ROLLBACK");
            mysql_stmt_close(stmtUpdate);
            return CostumeChangeFailCode::ServerError;
        }

        mysql_stmt_close(stmtUpdate);
        mysql_query(ConnPtr, "COMMIT");

        std::cout << "[UpdateEquipSlot] Success. userPk: " << userPk_
            << " slot: " << (int)slot_
            << " itemCode: " << itemCode_ << '\n';

        return CostumeChangeFailCode::None;
    }
    catch (const std::exception& e) {
        std::cerr << "[UpdateEquipSlot] Exception: " << e.what() << '\n';
        mysql_query(ConnPtr, "ROLLBACK");
        return CostumeChangeFailCode::ServerError;
    }
}