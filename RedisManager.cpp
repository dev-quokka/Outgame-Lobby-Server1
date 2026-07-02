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
    packetIDTable[(uint16_t)PACKET_ID::USER_LOBBY_CONNECT_REQUEST] = &RedisManager::UserConnect;


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
    catch (const  sw::redis::Error& e) {
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

// 받은 친구 요청 수락/거절 처리 함수
void RedisManager::ProcessFriendAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<FRIEND_ACTION_REQUEST*>(pPacket_);

    FRIEND_ACTION_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::FRIEND_ACTION_RESPONSE;
    res.PacketLength = sizeof(FRIEND_ACTION_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId), reqPacket->targetId, _TRUNCATE);

    auto me = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = me->GetPk();
    std::string myId = me->GetId();

    if (reqPacket->action == 0) { // 친구 요청 수락
        auto targetPk = MySQLManager::GetInstance().AcceptFriend(myPk, std::string(reqPacket->targetId));
        if (!targetPk.has_value()) {
            res.isSuccess = false;
            me->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }

        // 내 세션 캐시에 친구 추가
        me->AddFriend(*targetPk);

        // 상대에게 수락 알림 pub/sub
        // {"type":6,"data":{"targetPk":13,"senderPk":5}}
        std::string message =
            R"({"type":6,"data":{"targetPk":)"
            + std::to_string(*targetPk)
            + R"(,"senderPk":)" + std::to_string(myPk)
            + R"(}})";

        PublishToUsers({ *targetPk }, message);
    }
    else { // 친구 요청 거절/삭제
        auto targetPk = MySQLManager::GetInstance()
            .RemoveFriend(myPk, std::string(reqPacket->targetId));
        if (!targetPk.has_value()) {
            res.isSuccess = false;
            me->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }

        // 내 세션 캐시에서 친구 제거
        me->RemoveFriend(*targetPk);

        // 상대에게 삭제/거절 알림 pub/sub
        // {"type":7,"data":{"targetPk":13,"senderPk":5}}
        std::string message =
            R"({"type":7,"data":{"targetPk":)"
            + std::to_string(*targetPk)
            + R"(,"senderPk":)" + std::to_string(myPk)
            + R"(}})";

        PublishToUsers({ *targetPk }, message);
    }

    res.isSuccess = true;
    me->PushSendMsg(sizeof(res), (char*)&res);
}

void RedisManager::ProcessUserSearch(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<USER_SEARCH_REQUEST*>(pPacket_);

    USER_SEARCH_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::USER_SEARCH_RESPONSE;
    res.PacketLength = sizeof(USER_SEARCH_RESPONSE);

    // 본인 검색 막기
    if (std::string(reqPacket->userId) ==connUsersManager->FindUser(connObjNum_)->GetId()) {
        res.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 1. DB에서 유저 찾기
    auto result = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->userId));

    if (!result.has_value()) { // 매칭되는 유저 없음
        res.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 2. Redis에서 온라인 상태 조회
    try {
        std::string userKey = "user:" + std::to_string(result->userPk);
        auto state = redis->hget(userKey, "state");

        if (!state.has_value()) {
            res.onlineStatus = 0;  // 오프라인
        }
        else if (*state == "lobby") {
            res.onlineStatus = 1;
        }
        else if (*state == "ingame") {
            res.onlineStatus = 2;
        }
        else {
            res.onlineStatus = 0;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessUserSearch] Redis error: " << e.what() << '\n';
        res.onlineStatus = 0;  // 에러 시 오프라인으로 전송하자
    }

    // 3. 결과 전송
    strncpy_s(res.userId, sizeof(res.userId),result->userId, _TRUNCATE);
    res.userLevel = result->userLevel;
    res.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(res), (char*)&res);
}

void RedisManager::ProcessFriendRequest(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<FRIEND_REQUEST_REQUEST*>(pPacket_);

    FRIEND_REQUEST_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::FRIEND_REQUEST_RESPONSE;
    res.PacketLength = sizeof(FRIEND_REQUEST_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId),reqPacket->targetId, _TRUNCATE);

    auto me = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = me->GetPk();
    uint16_t myLevel = me->GetLevel();
    std::string myId = me->GetId();

    // 본인 검색 방지용
    if (std::string(reqPacket->targetId) == me->GetId()) {
        res.isSuccess = false;
        me->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    auto targetPk = MySQLManager::GetInstance().SendFriendRequest(myPk, std::string(reqPacket->targetId));
    if (!targetPk.has_value()) {
        res.isSuccess = false;
        me->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 세션 캐시에 추가 (친구 추가 받거나 요청한 사람 모두 해당 유저 상태 확인용)
    me->AddFriend(*targetPk);

    // pub/sub - A 정보를 메시지에 포함해서 B가 추가 조회 없이 바로 사용
    std::string message =
        R"({"type":8,"data":{"targetPk":)"
        + std::to_string(*targetPk)
        + R"(,"senderPk":)" + std::to_string(myPk)
        + R"(,"senderId":")" + myId + R"(")" 
        + R"(,"senderLevel":)" + std::to_string(myLevel)
        + R"(,"onlineStatus":1}})"; // 로비에 있으니까 항상 1

    PublishToUsers({ *targetPk }, message);

    res.isSuccess = true;
    me->PushSendMsg(sizeof(res), (char*)&res);
}

void RedisManager::PublishToUsers(const std::vector<uint32_t>& targetPks_, const std::string& eventMessage_) {
    if (targetPks_.empty()) return;

    try {
        // pipeline으로 서버 위치 조회
        auto pipe = redis->pipeline();
        for (auto pk : targetPks_) {
            pipe.hget("user:" + std::to_string(pk), "server");
        }
        auto replies = pipe.exec();

        // 서버별로 타겟 pk 그룹핑
        std::unordered_map<std::string, std::vector<uint32_t>> serverTargets;
        for (int i = 0; i < (int)targetPks_.size(); i++) {
            try {
                auto server = replies.get<sw::redis::OptionalString>(i);
                if (server.has_value()) {
                    serverTargets[*server].push_back(targetPks_[i]);
                }
            }
            catch (...) { continue; } // 예외 발생한 pk 하나만 건너뛰고 나머지는 계속 처리
        }

        // 기존 메시지의 data 안에 targets 삽입
        // 기존 형태: {"type":1,"data":{"userPk":13}}
        // 수정 형태: {"type":1,"data":{"userPk":13,"targets":[14,15, .....]}}

        // 서버별로 타겟 pk를 묶어서 한 번에 publish
        // 같은 서버에 친구가 N명 있어도 publish는 1번 -> Redis 왕복 최소화
        for (const auto& [server, pks] : serverTargets) {
            std::string targets = "[";
            for (int i = 0; i < (int)pks.size(); i++) {
                if (i > 0) targets += ",";
                targets += std::to_string(pks[i]);
            }
            targets += "]";

            std::string message = eventMessage_;
            auto pos = message.rfind("}}");  // 마지막 }} 찾기
            message.insert(pos, R"(,"targets":)" + targets);

            redis->publish(server + ":events", message);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[PublishToUsers] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyFriendOnline(uint32_t userPk_, const std::vector<uint32_t>& friendPks_) {
    if (friendPks_.empty()) return;

    // 메시지 구성
    std::string message = R"({"type":1,"data":{"userPk":)" + std::to_string(userPk_) + R"(}})";

    // PublishToUsers가 서버별로 묶어서 publish
    PublishToUsers(friendPks_, message);
}

void RedisManager::NotifyFriendOffline(uint32_t userPk_) {
    // 1. DB에서 친구 목록 조회
    auto friendsDB = MySQLManager::GetInstance().GetUserFriendsPks(userPk_);
    if (!friendsDB.has_value()) return;

    // 2. 친구인 것만 추리기
    std::vector<uint32_t> friendPks;
    for (const auto& f : friendsDB.value()) {
        friendPks.push_back(f);
    }
    if (friendPks.empty()) return;

    // 3. 메시지 구성
    std::string message = R"({"type":2,"data":{"userPk":)" + std::to_string(userPk_) + R"(}})";

    // 4. PublishToUsers가 알아서 서버별로 묶어서 publish (서버 정보 있는 애들)
    PublishToUsers(friendPks, message);
}


// ====================== UserState =======================

void RedisManager::UserConnect(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
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

    auto tempFD = MySQLManager::GetInstance().GetUserFriendsPks(userPk);
    if (!tempFD.has_value()) { // 친구 정보 내부 캐싱 실패시 접속 실패 반환
        std::cerr << "[ProcessLobbyConnect] GetUserFriendsPks failed. userPk: " << userPk << '\n';
        userConnRes.isSuccess = false;
        tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);
        return;
    }
    std::vector<uint32_t> friendsDB;
    friendsDB = tempFD.value();

    // 각 connUser 세션에 unordered_set으로 친구 pk 저장해두기
    tempUser->SetFriendPks(friendsDB);

    // 친구들에게 접속 알림 (실패해도 로그인 유지)
    NotifyFriendOnline(userPk, friendsDB);

    // PK,ID 세팅
    tempUser->SetPk(userPk);
    tempUser->SetId(tempId);

    // Redis 상태 갱신
    std::string userKey = "user:" + std::to_string(userPk);
    redis->hset(userKey, "state", "lobby");
    redis->expire(userKey, std::chrono::seconds(300));

    userConnRes.isSuccess = true;
    tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);

    std::cout << "[ProcessLobbyConnect] userId: " << reqPacket->userId << " userPk: " << userPk << '\n';
}

void RedisManager::UserDisConnect(uint16_t connObjNum_) {
    auto user = connUsersManager->FindUser(connObjNum_);
    uint32_t userPk = user->GetPk();

    // 친구들에게 오프라인 알림
    NotifyFriendOffline(userPk);

    auto pipe = redis->pipeline();

    //redis->hset(userInfokey, "userstate", "offline"); // Set user status to "offline" in Redis Cluster
    //redis->expire(equipmentkey, std::chrono::seconds(180)); // ttl 3분 설정
    //redis->expire(consumablekey, std::chrono::seconds(180)); // ttl 3분 설정
    //redis->expire(materialkey, std::chrono::seconds(180)); // ttl 3분 설정

    pipe.exec();


    connUsersManager->FindUser(connObjNum_)->DelFriendPks();
    connUsersManager->DelPkToObjNum(userPk);
}

void RedisManager::SendFriendRequestToUser(uint32_t targetPk_, uint32_t senderPk_, const std::string& senderId_, uint16_t senderLevel_, uint8_t onlineStatus_) {
    FRIEND_REQUEST_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::FRIEND_REQUEST_NOTIFY;
    notify.PacketLength = sizeof(notify);

    auto tempObjNum = connUsersManager->GetObjNumByPk(targetPk_); // 친구 접속 상태 받을 현재 서버에 있는 유저
    auto temoUser = connUsersManager->FindUser(tempObjNum);

    temoUser->AddFriend(senderPk_);

    notify.senderPk = senderPk_;
    strncpy_s(notify.senderId, sizeof(notify.senderId), senderId_.c_str(), _TRUNCATE);
    notify.senderLevel = senderLevel_;
    notify.onlineStatus = onlineStatus_;
    temoUser->PushSendMsg(sizeof(notify), (char*)&notify);
}

// 펍섭으로 특정 유저 접속을 받고 해당 유저 친구들에게 온/오프 상태를 알리는 함수
// targetPk_: 요청 받을 유저 pk, friendPk_: 요청 보낸 친구 pk
void RedisManager::SendFriendStatusToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t status_) {
    FRIEND_STATUS_NOTIFY friendNotifyPacket;
    friendNotifyPacket.PacketId = (uint16_t)PACKET_ID::FRIEND_STATUS_NOTIFY;
    friendNotifyPacket.PacketLength = sizeof(FRIEND_STATUS_NOTIFY);
    friendNotifyPacket.friendPk = friendPk_;
    friendNotifyPacket.onlineStatus = status_;

    auto tempObjNum = connUsersManager->GetObjNumByPk(targetPk_); // 친구 접속 상태 받을 현재 서버에 있는 유저 
    connUsersManager->FindUser(tempObjNum)->PushSendMsg(sizeof(friendNotifyPacket), (char*)&friendNotifyPacket);
}

void RedisManager::SendFriendAcceptToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t accept_) {
    auto tempObjNum = connUsersManager->GetObjNumByPk(targetPk_); // 친구 요청 상태 받을 현재 서버에 있는 유저 
    if (!tempObjNum) return;  // 이 서버에 없는 유저

    auto tempConnUser = connUsersManager->FindUser(tempObjNum);

    // 세션 캐시 갱신
    if (accept_ == 0) { // 수락/친구 추가

    }
    else { // 거절/친구 제거
        tempConnUser->RemoveFriend(friendPk_);
    }

    FRIEND_ACCEPT_NOTIFY friendNotifyPacket;
    friendNotifyPacket.PacketId = (uint16_t)PACKET_ID::FRIEND_ACCEPT_NOTIFY;
    friendNotifyPacket.PacketLength = sizeof(friendNotifyPacket);
    friendNotifyPacket.friendPk = friendPk_;
    friendNotifyPacket.accept = accept_;

    tempConnUser->PushSendMsg(sizeof(friendNotifyPacket), (char*)&friendNotifyPacket);
}


// ====================== Friend =======================

// 친구 요청 삭제 or 받기 => 완료

// 친구 목록 확인 (1. 상태(오프, 온라인 - 로비, 온라인 - 게임중), 2. 레벨, 3. 파티 정보(없음 or 파티 현재 인원), 4. 참가하기, 5. 초대하기)

// 유저 검색 (1. 상태, 2. 레벨, 3. 참가하기, 4. 초대하기)



// ====================== Party =======================

// 파티 초대하기 (친구목록에서 초대하기)

// 파티 참가하기 (친구목록에서 따라가기)



// ====================== Inventory =======================

// 코스튬 변경 (파티 없으면 그냥 변경만, 파티 있으면 나머지 파티원한테도 펍섭으로 전달)

// 인벤토리 확인