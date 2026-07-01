#include "RedisManager.h"

RedisManager& RedisManager::GetInstance() {
    static RedisManager instance;  // 싱글톤 초기화
    return instance;
}

sw::redis::Redis& RedisManager::GetRedis() {
    if (!redis) {
        throw std::runtime_error("[Redis] Not connected. Call Connect() first.");
    }
    return *redis;
}


void RedisManager::Init(const uint16_t RedisThreadCnt_) {

    // -------------------- SET PACKET HANDLERS ----------------------
    packetIDTable = std::unordered_map<uint16_t, RECV_PACKET_FUNCTION>();

    // LOGIN
    packetIDTable[(uint16_t)PACKET_ID::USER_LOBBY_CONNECT_REQUEST] = &RedisManager::ProcessLobbyConnect;


    RedisRun(RedisThreadCnt_);
}

// ===================== PACKET MANAGEMENT =====================

void RedisManager::PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_) {
    ConnUser* TempConnUser = connUsersManager->FindUser(connObjNum_);
    TempConnUser->WriteRecvData(recvData_, size_); // Push Data in Circualr Buffer
    DataPacket tempD(size_, connObjNum_);
    procSktQueue.push(tempD);
}


// ====================== REDIS MANAGEMENT =====================

void RedisManager::RedisRun(const uint16_t RedisThreadCnt_) { // Connect Redis Server
    try {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        opts.socket_timeout = std::chrono::seconds(10);
        opts.keep_alive = true;

        redis = std::make_unique<sw::redis::Redis>(opts);
        redis->ping();
        std::cout << "[Redis] Connected to " << host << ":" << port << std::endl;

        CreateRedisThread(RedisThreadCnt_);

    }
    catch (const  sw::redis::Error& err) {
        std::cerr << "[Redis] Connection failed: " << e.what() << std::endl;
        throw;  // 연결 실패하면 서버 시작 중단
    }
}

bool RedisManager::CreateRedisThread(const uint16_t RedisThreadCnt_) {
    redisRun = true;

    try {
        for (int i = 0; i < RedisThreadCnt_; i++) {
            redisThreads.emplace_back(std::thread([this]() { RedisThread(); }));
        }
    }
    catch (const std::system_error& e) {
        std::cerr << "Create Redis Thread Failed : " << e.what() << std::endl;
        return false;
    }

    return true;
}

void RedisManager::RedisThread() {
    DataPacket tempD(0, 0);
    ConnUser* TempConnUser = nullptr;
    char tempData[1024] = { 0 };

    while (redisRun) {
        if (procSktQueue.pop(tempD)) {
            std::memset(tempData, 0, sizeof(tempData));
            TempConnUser = connUsersManager->FindUser(tempD.connObjNum); // Find User
            PacketInfo packetInfo = TempConnUser->ReadRecvData(tempData, tempD.dataSize); // GetData
            (this->*packetIDTable[packetInfo.packetId])(packetInfo.connObjNum, packetInfo.dataSize, packetInfo.pData); // Proccess Packet
        }
        else { // Empty Queue
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}


bool RedisManager::VerifyUserToken(const std::string& userId_, const char* token_, uint32_t& outUserPk_) {
    try {
        std::string key = "jwtcheck:{" + userId_ + "}";

        // userId + token으로 Redis 조회
        auto result = redis->hget(key, std::string(token_));
        if (!result.has_value()) {
            std::cerr << "[VerifyUserToken] Token not found. userId: " << userId_ << '\n';
            return false;
        }

        // userPk 추출
        outUserPk_ = static_cast<uint32_t>(std::stoul(*result));

        // 검증 후 즉시 삭제
        redis->del(key);

        std::cout << "[VerifyUserToken] Success. userId: " << userId_ << " userPk: " << outUserPk_ << '\n';
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[VerifyUserToken] Exception: " << e.what() << '\n';
        return false;
    }
}

void RedisManager::ProcessFriendAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<FRIEND_ACTION_REQUEST*>(pPacket_);

    FRIEND_ACTION_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::FRIEND_ACTION_RESPONSE;
    res.PacketLength = sizeof(FRIEND_ACTION_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId),reqPacket->targetId, _TRUNCATE);

    // 내 pk 가져오기
    uint32_t myPk = connUsersManager->FindUser(connObjNum_)->GetPk();

    // DB UPDATE + targetPk 반환
    auto targetPk = MySQLManager::GetInstance().AcceptFriend(myPk, std::string(reqPacket->targetId));

    if (!targetPk.has_value()) {
        res.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    res.isSuccess = true;

    // Redis에서 상대 온라인 확인
    std::string userKey = "user:" + std::to_string(*targetPk);
    auto state = redis->hget(userKey, "state");

    if (state.has_value()) {
        // 온라인은 pub/sub로 수락 알림
        // B의 친구목록에 A 추가되도록
        std::string myId = connUsersManager->FindUser(connObjNum_)->GetId();
        std::string message =
            R"({"type":6,"data":{"userId":")"
            + myId 
            + R"("}})";
    }

    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(res), (char*)&res);
}

void RedisManager::PublishToUsers(const std::vector<uint32_t>& targetPks_, const std::string& message_) {
    if (targetPks_.empty()) return;

    try {
        // pipeline으로 받아온 pk의 서버 위치 한 번에 조회
        auto pipe = redis->pipeline();
        for (auto pk : targetPks_) {
            pipe.hget("user:" + std::to_string(pk), "server");
        }
        auto replies = pipe.exec();

        // 온라인인(서버 정보 있는) 유저들의 서버 확인
        std::unordered_set<std::string> targetServers;
        for (int i = 0; i < (int)targetPks_.size(); i++) {
            try {
                auto server = replies.get<sw::redis::OptionalString>(i);
                if (server.has_value()) {
                    targetServers.insert(*server);
                }
            }
            catch (...) { // 예외 발생한 pk 하나만 건너뛰고 나머지는 계속 처리
                continue;
            }
        }

        // 서버별로 한 번씩만 publish (중복 전송 방지)
        for (const auto& server : targetServers) {
            redis->publish(server + ":events", message_);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[PublishToUsers] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyFriendOffline(uint32_t userPk_) {
    // 1. DB에서 친구 목록 조회
    auto friendsDB = MySQLManager::GetInstance().GetUserFriendsDB(userPk_);
    if (!friendsDB.has_value()) return;

    // 2. 친구인 것만 추리기
    std::vector<uint32_t> friendPks;
    for (const auto& f : friendsDB.value()) {
        if (f.friendStatus == 1) friendPks.push_back(f.friendPk);
    }
    if (friendPks.empty()) return;

    // 3. 메시지 구성
    std::string message = R"({"type":2,"data":{"userPk":)"
        + std::to_string(userPk_) + R"(}})";

    // 4. PublishToUsers가 알아서 서버별로 묶어서 publish (서버 정보 있는 애들)
    PublishToUsers(friendPks, message);
}


// ====================== UserState =======================

void RedisManager::UserDisConnect(uint16_t connObjNum_) {
    auto user = connUsersManager->FindUser(connObjNum_);
    uint32_t userPk = user->GetPk();

    // 친구들에게 오프라인 알림
    NotifyFriendOffline(userPk);

    auto pipe = redis->pipeline();

    redis->hset(userInfokey, "userstate", "offline"); // Set user status to "offline" in Redis Cluster
    redis->expire(equipmentkey, std::chrono::seconds(180)); // ttl 3분 설정
    redis->expire(consumablekey, std::chrono::seconds(180)); // ttl 3분 설정
    redis->expire(materialkey, std::chrono::seconds(180)); // ttl 3분 설정

    pipe.exec();
}

void RedisManager::ProcessLobbyConnect(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<USER_LOBBY_CONNECT_REQUEST*>(pPacket_);

    USER_LOBBY_CONNECT_RESPONSE userConnRes;
    userConnRes.PacketId = (uint16_t)PACKET_ID::USER_LOBBY_CONNECT_RESPONSE;
    userConnRes.PacketLength = sizeof(USER_LOBBY_CONNECT_RESPONSE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    auto tempId = std::string(reqPacket->userId);

    // userId + token으로 검증
    uint32_t userPk = 0;
    if (!VerifyUserToken(tempId,reqPacket->token,userPk)) {
        userConnRes.isSuccess = false;
        tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);
        return;
    }

    // PK,ID 세팅
    tempUser->SetPk(userPk);
    tempUser->SetId(tempId);

    connUsersManager

    // Redis 상태 갱신
    std::string userKey = "user:" + std::to_string(userPk);
    redis->hset(userKey, "state", "lobby");
    redis->expire(userKey, std::chrono::seconds(300));

    userConnRes.isSuccess = true;
    tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);

    std::cout << "[ProcessLobbyConnect] userId: " << reqPacket->userId << " userPk: " << userPk << '\n';
}



// ====================== Friend =======================

// 친구 요청 삭제 or 받기

// 친구 목록 확인 (1. 상태(오프, 온라인 - 로비, 온라인 - 게임중), 2. 레벨, 3. 파티 정보(없음 or 파티 현재 인원), 4. 참가하기, 5. 초대하기)

// 유저 검색 (1. 상태, 2. 레벨, 3. 참가하기, 4. 초대하기)



// ====================== Party =======================

// 파티 초대하기 (친구목록에서 초대하기)

// 파티 참가하기 (친구목록에서 따라가기)



// ====================== Inventory =======================

// 코스튬 변경 (파티 없으면 그냥 변경만, 파티 있으면 나머지 파티원한테도 펍섭으로 전달)

// 인벤토리 확인