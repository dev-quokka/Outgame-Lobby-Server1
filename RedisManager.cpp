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



// ============================================ 클라 요청 처리 ============================================

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

void RedisManager::ProcessCostumeChange(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<COSTUME_CHANGE_REQUEST*>(pPacket_);

    COSTUME_CHANGE_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::COSTUME_CHANGE_RESPONSE;
    res.PacketLength = sizeof(COSTUME_CHANGE_RESPONSE);
    res.slot = reqPacket->slot;
    res.itemCode = reqPacket->itemCode;

    auto me = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = me->GetPk();
    std::string myId = me->GetId();

    // DB - 인벤 확인 + 슬롯 업데이트
    auto failCode = MySQLManager::GetInstance().UpdateEquipSlot(
        myPk, reqPacket->slot, reqPacket->itemCode);

    if (failCode != CostumeChangeFailCode::None) {
        res.isSuccess = false;
        res.failCode = (uint8_t)failCode.value();
        me->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // Redis user:{pk}:equip 갱신
    try {
        std::string slotName;
        switch (reqPacket->slot) {
        case 1: slotName = "head"; break;
        case 2: slotName = "body"; break;
        case 3: slotName = "legs"; break;
        case 4: slotName = "feet"; break;
        default: slotName = "head"; break;
        }
        std::string equipKey = "user:" + std::to_string(myPk) + ":equip";
        redis->hset(equipKey, slotName, std::to_string(reqPacket->itemCode));
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessCostumeChange] Redis error: "
            << e.what() << '\n';
        // Redis 실패해도 DB는 성공했으니 계속 진행
    }

    res.isSuccess = true;
    res.failCode = (uint8_t)CostumeChangeFailCode::None;
    me->PushSendMsg(sizeof(res), (char*)&res);
}

void RedisManager::ProcessPartyFollow(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<PARTY_FOLLOW_REQUEST*>(pPacket_);

    PARTY_FOLLOW_RESPONSE partyFollowRes;
    partyFollowRes.PacketId = (uint16_t)PACKET_ID::PARTY_FOLLOW_RESPONSE;
    partyFollowRes.PacketLength = sizeof(PARTY_FOLLOW_RESPONSE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();

    // 이미 파티에 있으면 불가
    if (tempUser->GetPartId() != 0) {
        partyFollowRes.isSuccess = false;
        partyFollowRes.failCode = (uint8_t)PartyFailCode::AlreadyInParty;
        tempUser->PushSendMsg(sizeof(partyFollowRes), (char*)&partyFollowRes);
        return;
    }

    try {
        // B의 pk 조회 (Redis에서 userId에서 pk 매핑 없으니 DB 조회)
        // 근데 친구라면 이미 friendPks에 있을 수 있음
        // 일단 DB로 조회
        auto targetInfo = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->targetId));
        if (!targetInfo.has_value()) {
            partyFollowRes.isSuccess = false;
            partyFollowRes.failCode = (uint8_t)PartyFailCode::UserNotFound;
            tempUser->PushSendMsg(sizeof(partyFollowRes), (char*)&partyFollowRes);
            return;
        }
        uint32_t targetPk = targetInfo->userPk;

        // B의 partyId 확인
        std::string targetUserKey = "user:" + std::to_string(targetPk);
        auto targetPartyIdStr = redis->hget(targetUserKey, "partyId");

        uint32_t partyId = 0;

        if (!targetPartyIdStr.has_value() || *targetPartyIdStr == "0") {
            // B 파티 없으면 새 파티 생성 (B가 파티장)
            partyId = static_cast<uint32_t>(redis->incr("party:counter"));

            std::string membersKey = "party:" + std::to_string(partyId) + ":members";
            std::string leaderKey = "party:" + std::to_string(partyId) + ":leader";

            // B를 파티장 + 첫 멤버로
            redis->sadd(membersKey, std::to_string(targetPk));
            redis->set(leaderKey, std::to_string(targetPk));
            redis->hset(targetUserKey, "partyId", std::to_string(partyId));

            // B 세션 partyId 갱신 (B가 이 서버에 있으면)
            auto targetUser = connUsersManager->FindUser(connUsersManager->GetObjNumByPk(targetPk));
            if (targetUser) targetUser->SetPartyId(partyId);
        }
        else {
            // B 파티 있으면 기존 파티에 입장
            partyId = std::stoul(*targetPartyIdStr);
        }

        // 파티 인원 확인 (최대 4명)
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        auto memberCount = redis->scard(membersKey);
        if (memberCount >= 4) {
            partyFollowRes.isSuccess = false;
            partyFollowRes.failCode = (uint8_t)PartyFailCode::PartyFull;
            tempUser->PushSendMsg(sizeof(partyFollowRes), (char*)&partyFollowRes);
            return;
        }

        std::string userKey = "user:" + std::to_string(myPk);

        // A를 파티에 추가
        redis->sadd(membersKey, std::to_string(myPk));
        redis->hset(userKey, "partyId", std::to_string(partyId));
        tempUser->SetPartyId(partyId);

        // A에게 파티 전체 정보 전달
        SendPartyInfo(connObjNum_, partyId);

        // 기존 파티원들에게 A 입장 알림
        NotifyPartyJoin(myPk, partyId);

        partyFollowRes.isSuccess = true;
        partyFollowRes.partyId = partyId;
        tempUser->PushSendMsg(sizeof(partyFollowRes), (char*)&partyFollowRes);
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessPartyFollow] Error: " << e.what() << '\n';
        partyFollowRes.isSuccess = false;
        partyFollowRes.failCode = (uint8_t)PartyFailCode::ServerError;
        tempUser->PushSendMsg(sizeof(partyFollowRes), (char*)&partyFollowRes);
    }
}

void RedisManager::ProcessPartyInvite(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {

    auto reqPacket = reinterpret_cast<PARTY_INVITE_REQUEST*>(pPacket_);

    PARTY_INVITE_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::PARTY_INVITE_RESPONSE;
    res.PacketLength = sizeof(PARTY_INVITE_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId),
        reqPacket->targetId, _TRUNCATE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    // 파티 있으면 인원 확인하기
    if (partyId != 0) {
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        auto memberCount = redis->scard(membersKey);
        if (memberCount >= 4) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::PartyFull;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }
    }

    // B의 pk 조회
    auto targetInfo = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->targetId));
    if (!targetInfo.has_value()) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::UserNotFound;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }
    uint32_t targetPk = targetInfo->userPk;

    // B에게 초대 알림 pub/sub
    uint8_t memberCount = 0;
    if (partyId != 0) {
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        memberCount = static_cast<uint8_t>(redis->scard(membersKey));
    }

    std::string message =
        R"({"type":9,"data":{"targetPk":)"
        + std::to_string(targetPk)
        + R"(,"senderPk":)" + std::to_string(myPk)
        + R"(,"senderId":")" + tempUser->GetId() + R"(")"
        + R"(,"senderLevel":)" + std::to_string(tempUser->GetLevel())
        + R"(,"partyId":)" + std::to_string(partyId)
        + R"(,"memberCount":)" + std::to_string(memberCount)
        + R"(}})";

    PublishToUsers({ targetPk }, message);

    res.isSuccess = true;
    tempUser->PushSendMsg(sizeof(res), (char*)&res);

    std::cout << "[ProcessPartyInvite] myPk: " << myPk << " targetPk: " << targetPk << '\n';
}

void RedisManager::ProcessPartyInviteAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<PARTY_INVITE_ACCEPT_REQUEST*>(pPacket_);

    PARTY_INVITE_ACCEPT_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::PARTY_INVITE_ACCEPT_RESPONSE;
    res.PacketLength = sizeof(PARTY_INVITE_ACCEPT_RESPONSE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();

    // A의 pk 조회
    auto senderInfo = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->senderId));
    if (!senderInfo.has_value()) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::UserNotFound;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }
    uint32_t senderPk = senderInfo->userPk;

    if (reqPacket->accept == 1) {
        // A에게 거절 알림 pub/sub
        std::string message =
            R"({"type":9,"data":{"targetPk":)"
            + std::to_string(senderPk)
            + R"(,"senderId":")" + tempUser->GetId() + R"(")"
            + R"(,"reject":1}})";
        PublishToUsers({ senderPk }, message);

        res.isSuccess = true;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 수락 -> A의 현재 파티 확인
    std::string senderUserKey = "user:" + std::to_string(senderPk);
    auto senderPartyIdStr = redis->hget(senderUserKey, "partyId");

    uint32_t partyId = 0;
    std::string userKey = "user:" + std::to_string(myPk);

    try {
        // B가 기존 파티 있으면 먼저 탈퇴처리
        if (tempUser->GetPartId() != 0) {
            LeavePartyInternal(myPk, tempUser->GetPartId());
        }

        if (!senderPartyIdStr.has_value() || *senderPartyIdStr == "0") {
            // A 파티 없음 -> 새 파티 생성 (A가 파티장)
            partyId = static_cast<uint32_t>(redis->incr("party:counter"));

            std::string membersKey = "party:" + std::to_string(partyId) + ":members";
            std::string leaderKey = "party:" + std::to_string(partyId) + ":leader";

            redis->sadd(membersKey, std::to_string(senderPk));
            redis->set(leaderKey, std::to_string(senderPk));
            redis->hset(senderUserKey, "partyId", std::to_string(partyId));

            // A 세션 갱신
            ConnUser* senderUser = connUsersManager->FindUserByPk(senderPk);
            if (senderUser) senderUser->SetPartyId(partyId);
        }
        else {
            partyId = std::stoul(*senderPartyIdStr);
        }

        // 인원 확인
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        auto memberCount = redis->scard(membersKey);
        if (memberCount >= 4) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::PartyFull;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }

        // B를 파티에 추가
        redis->sadd(membersKey, std::to_string(myPk));
        redis->hset(userKey, "partyId", std::to_string(partyId));
        tempUser->SetPartyId(partyId);

        // B에게 파티 전체 정보 전달
        SendPartyInfo(connObjNum_, partyId);

        // 기존 파티원들에게 B 입장 알림
        NotifyPartyJoin(myPk, partyId);

        res.isSuccess = true;
        res.partyId = partyId;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessPartyInviteAccept] Error: " << e.what() << '\n';
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::ServerError;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
}

void RedisManager::ProcessPartyLeave(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {

    PARTY_LEAVE_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::PARTY_LEAVE_RESPONSE;
    res.PacketLength = sizeof(PARTY_LEAVE_RESPONSE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    if (partyId == 0) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::NotInParty;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    try {
        LeavePartyInternal(myPk, partyId);
        res.isSuccess = true;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessPartyLeave] Error: " << e.what() << '\n';
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::ServerError;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
}

void RedisManager::ProcessPartyKick(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {

    auto reqPacket = reinterpret_cast<PARTY_KICK_REQUEST*>(pPacket_);

    PARTY_KICK_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::PARTY_KICK_RESPONSE;
    res.PacketLength = sizeof(PARTY_KICK_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId),reqPacket->targetId, _TRUNCATE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    // 파티에 없으면 불가
    if (partyId == 0) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::NotInParty;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 파티장인지 확인
    if (!IsPartyLeader(myPk, partyId)) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::NotLeader;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    try {
        // 강퇴 대상 pk 조회
        auto targetInfo = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->targetId));
        if (!targetInfo.has_value()) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::UserNotFound;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }
        uint32_t targetPk = targetInfo->userPk;

        // 대상이 이 파티 멤버인지 확인
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        bool isMember = redis->sismember(membersKey, std::to_string(targetPk));
        if (!isMember) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::NotInParty;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }

        // members에서 제거
        redis->srem(membersKey, std::to_string(targetPk));

        // B 세션 + Redis partyId 초기화
        std::string targetUserKey = "user:" + std::to_string(targetPk);
        redis->hset(targetUserKey, "partyId", "0");

        ConnUser* targetUser = connUsersManager->FindUserByPk(targetPk);
        if (targetUser) targetUser->SetPartyId(0);

        // 강퇴된 B에게 알림 (같은 서버에 있으면 직접, 다른 서버면 pub/sub)
        // B + 남은 파티원들 모두에게 알림
        NotifyPartyKick(targetPk, partyId);

        res.isSuccess = true;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);

        std::cout << "[ProcessPartyKick] myPk: " << myPk
            << " targetPk: " << targetPk
            << " partyId: " << partyId << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessPartyKick] Error: " << e.what() << '\n';
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::ServerError;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
}

void RedisManager::ProcessPartyDelegate(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto reqPacket = reinterpret_cast<PARTY_DELEGATE_REQUEST*>(pPacket_);

    PARTY_DELEGATE_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::PARTY_DELEGATE_RESPONSE;
    res.PacketLength = sizeof(PARTY_DELEGATE_RESPONSE);
    strncpy_s(res.targetId, sizeof(res.targetId), reqPacket->targetId, _TRUNCATE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    // 파티에 없으면 불가
    if (partyId == 0) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::NotInParty;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    // 파티장인지 확인
    if (!IsPartyLeader(myPk, partyId)) {
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::NotLeader;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
        return;
    }

    try {
        // 새 파티장 pk 조회
        auto targetInfo = MySQLManager::GetInstance().SearchUser(std::string(reqPacket->targetId));
        if (!targetInfo.has_value()) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::UserNotFound;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }
        uint32_t targetPk = targetInfo->userPk;

        // 대상이 이 파티 멤버인지 확인
        std::string membersKey = "party:" + std::to_string(partyId) + ":members";
        bool isMember = redis->sismember(membersKey, std::to_string(targetPk));
        if (!isMember) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::NotInParty;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }

        // 파티장 갱신
        std::string leaderKey = "party:" + std::to_string(partyId) + ":leader";
        redis->set(leaderKey, std::to_string(targetPk));

        // 파티원들에게 알림
        NotifyPartyDelegate(targetPk, partyId);

        res.isSuccess = true;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);

        std::cout << "[ProcessPartyDelegate] myPk: " << myPk
            << " newLeaderPk: " << targetPk
            << " partyId: " << partyId << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "[ProcessPartyDelegate] Error: " << e.what() << '\n';
        res.isSuccess = false;
        res.failCode = (uint8_t)PartyFailCode::ServerError;
        tempUser->PushSendMsg(sizeof(res), (char*)&res);
    }
}

void RedisManager::ProcessMatchStart(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    MATCH_START_RESPONSE res;
    res.PacketId = (uint16_t)PACKET_ID::MATCH_START_RESPONSE;
    res.PacketLength = sizeof(MATCH_START_RESPONSE);

    auto tempUser = connUsersManager->FindUser(connObjNum_);
    uint32_t myPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    // 파티 있으면 파티장인지 확인
    if (partyId != 0) {
        if (!IsPartyLeader(myPk, partyId)) {
            res.isSuccess = false;
            res.failCode = (uint8_t)PartyFailCode::NotLeader;
            tempUser->PushSendMsg(sizeof(res), (char*)&res);
            return;
        }
    }

    res.isSuccess = true;
    tempUser->PushSendMsg(sizeof(res), (char*)&res);

    // 파티 있으면 파티원들에게도 매칭 시작 알림
    if (partyId != 0) {
        NotifyMatchStart(myPk, partyId);
    }

    std::cout << "[ProcessMatchStart] myPk: " << myPk
        << " partyId: " << partyId << '\n';
}




// ============================================ REDIS Pub/Sub 발행 ============================================

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

void RedisManager::NotifyCostumeChangeToParty(uint32_t userPk_, const std::string& userId_,uint32_t partyId_,uint8_t slot_, uint32_t itemCode_) {
    try {
        // 파티원 pk 목록 조회
        std::string memberKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(memberKey,
            std::inserter(memberPkStrs, memberPkStrs.begin()));

        std::vector<uint32_t> memberPks;
        for (const auto& s : memberPkStrs) {
            uint32_t pk = std::stoul(s);
            if (pk != userPk_) memberPks.push_back(pk);  // 본인은 제외하기
        }
        if (memberPks.empty()) return;

        std::string message =
            R"({"type":3,"data":{"userPk":)"
            + std::to_string(userPk_)
            + R"(,"userId":")" + userId_ + R"(")"
            + R"(,"slot":)" + std::to_string(slot_)
            + R"(,"itemCode":)" + std::to_string(itemCode_)
            + R"(}})";

        PublishToUsers(memberPks, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyCostumeChangeToParty] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyPartyJoin(uint32_t newUserPk_, uint32_t partyId_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey, std::inserter(memberPkStrs, memberPkStrs.begin()));

        // 새 멤버 본인 제외한 파티원들
        std::vector<uint32_t> otherMembers;
        for (const auto& s : memberPkStrs) {
            uint32_t pk = std::stoul(s);
            if (pk != newUserPk_) otherMembers.push_back(pk);
        }
        if (otherMembers.empty()) return;

        // 새 멤버 정보
        ConnUser* newUser = connUsersManager->FindUserByPk(newUserPk_);
        if (!newUser) return;

        // 새 멤버 코스튬 조회 (Redis에 캐싱돼있음)
        std::string equipKey = "user:" + std::to_string(newUserPk_) + ":equip";
        uint32_t head = 0, body = 0, legs = 0, feet = 0;
        try {
            std::unordered_map<std::string, std::string> equip;
            redis->hgetall(equipKey, std::inserter(equip, equip.begin()));
            if (equip.count("head")) head = std::stoul(equip["head"]);
            if (equip.count("body")) body = std::stoul(equip["body"]);
            if (equip.count("legs")) legs = std::stoul(equip["legs"]);
            if (equip.count("feet")) feet = std::stoul(equip["feet"]);
        }
        catch (...) {}

        std::string message =
            R"({"type":10,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(,"userPk":)" + std::to_string(newUserPk_)
            + R"(,"userId":")" + newUser->GetId() + R"(")"
            + R"(,"userLevel":)" + std::to_string(newUser->GetLevel())
            + R"(,"head":)" + std::to_string(head)
            + R"(,"body":)" + std::to_string(body)
            + R"(,"legs":)" + std::to_string(legs)
            + R"(,"feet":)" + std::to_string(feet)
            + R"(}})";

        PublishToUsers(otherMembers, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyPartyJoin] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyPartyLeave(uint32_t userPk_, uint32_t partyId_, uint32_t newLeaderPk_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey, std::inserter(memberPkStrs, memberPkStrs.begin()));

        std::vector<uint32_t> remainMembers;
        for (const auto& s : memberPkStrs) {
            remainMembers.push_back(std::stoul(s));
        }
        if (remainMembers.empty()) return;

        std::string message =
            R"({"type":11,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(,"userPk":)" + std::to_string(userPk_)
            + R"(,"newLeaderPk":)" + std::to_string(newLeaderPk_)
            + R"(}})";

        PublishToUsers(remainMembers, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyPartyLeave] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyPartyKick(uint32_t targetPk_, uint32_t partyId_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey,
            std::inserter(memberPkStrs, memberPkStrs.begin()));

        // 남은 파티원 목록
        std::vector<uint32_t> targets;
        for (const auto& s : memberPkStrs) {
            targets.push_back(std::stoul(s));
        }
        // 강퇴된 유저도 알림 받아야 함
        targets.push_back(targetPk_);

        std::string message =
            R"({"type":12,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(,"userPk":)" + std::to_string(targetPk_)
            + R"(}})";

        PublishToUsers(targets, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyPartyKick] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyPartyDelegate(uint32_t newLeaderPk_, uint32_t partyId_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey,
            std::inserter(memberPkStrs, memberPkStrs.begin()));

        std::vector<uint32_t> targets;
        for (const auto& s : memberPkStrs) {
            targets.push_back(std::stoul(s));
        }
        if (targets.empty()) return;

        std::string message =
            R"({"type":13,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(,"newLeaderPk":)" + std::to_string(newLeaderPk_)
            + R"(}})";

        PublishToUsers(targets, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyPartyDelegate] Error: " << e.what() << '\n';
    }
}

void RedisManager::NotifyPartyMemberStatus(uint32_t userPk_, uint32_t partyId_, uint8_t onlineStatus_) {
    try {
        std::string membersKey = "party:" +
            std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey,
            std::inserter(memberPkStrs, memberPkStrs.begin()));

        std::vector<uint32_t> otherMembers;
        for (const auto& s : memberPkStrs) {
            uint32_t pk = std::stoul(s);
            if (pk != userPk_) otherMembers.push_back(pk);
        }
        if (otherMembers.empty()) return;

        std::string message =
            R"({"type":14,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(,"userPk":)" + std::to_string(userPk_)
            + R"(,"onlineStatus":)" + std::to_string(onlineStatus_)
            + R"(}})";

        PublishToUsers(otherMembers, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyPartyMemberStatus] Error: "
            << e.what() << '\n';
    }
}

void RedisManager::NotifyMatchStart(uint32_t leaderPk_, uint32_t partyId_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey,
            std::inserter(memberPkStrs, memberPkStrs.begin()));

        // 파티장 제외한 파티원들
        std::vector<uint32_t> otherMembers;
        for (const auto& s : memberPkStrs) {
            uint32_t pk = std::stoul(s);
            if (pk != leaderPk_) otherMembers.push_back(pk);
        }
        if (otherMembers.empty()) return;

        std::string message =
            R"({"type":5,"data":{"partyId":)"
            + std::to_string(partyId_)
            + R"(}})";

        PublishToUsers(otherMembers, message);
    }
    catch (const std::exception& e) {
        std::cerr << "[NotifyMatchStart] Error: " << e.what() << '\n';
    }
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
    // 각 connUser 세션에 unordered_set으로 친구 pk 저장해두기
    tempUser->SetFriendPks(tempFD.value());

    // 친구들에게 접속 알림 (실패해도 로그인 유지)
    NotifyFriendOnline(userPk, tempFD.value());

    // 레벨/경험치 조회 + 세션 캐싱
    auto tempSessionInfo = MySQLManager::GetInstance().GetUserSessionInfo(userPk);
    if (!tempSessionInfo.has_value()) {
        std::cerr << "[UserConnect] GetUserSessionInfo failed. userPk: " << userPk << '\n';
        userConnRes.isSuccess = false;
        tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);
        return;
    }

    // PK,ID, 레벨, 경험치, 파티Id 세팅
    tempUser->SetPk(userPk);
    tempUser->SetId(tempId);
    tempUser->SetLevel(tempSessionInfo->userLevel);
    tempUser->SetExp(tempSessionInfo->userExp);

    // Redis에서 기존 파티 상태 확인
    std::string userKey = "user:" + std::to_string(userPk);
    uint32_t currentPartyId = 0;
    try {
        auto partyIdStr = redis->hget(userKey, "partyId");
        if (partyIdStr.has_value() && *partyIdStr != "0") {
            uint32_t tempPartyId = std::stoul(*partyIdStr);

            // party:members에 내 pk 있는지 확인
            std::string membersKey = "party:" + std::to_string(tempPartyId) + ":members";
            bool isMember = redis->sismember(membersKey, std::to_string(userPk));

            if (isMember) { // 파티 유지
                currentPartyId = tempPartyId;
                std::cout << "[UserConnect] partyId restored: " << currentPartyId << '\n';
            }
            else { // 파티 해제됐거나 강퇴됨
                redis->hset(userKey, "partyId", "0");
                std::cout << "[UserConnect] partyId cleared. userPk: " << userPk << '\n';
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[UserConnect] partyId check error: " << e.what() << '\n';
    }
    tempUser->SetPartyId(currentPartyId);

    if (currentPartyId != 0) {
        SendPartyInfo(connObjNum_, currentPartyId);       
        NotifyPartyMemberStatus(userPk, currentPartyId, 1); // 파티원들에게 재접속 알림
    }

    // Redis 상태 갱신
    std::unordered_map<std::string, std::string> fields = {
        {"state",   "lobby"},
        {"partyId", std::to_string(currentPartyId)},
        {"level",   std::to_string(tempSessionInfo->userLevel)},
        {"exp",     std::to_string(tempSessionInfo->userExp)}
    };
    redis->hset(userKey, fields.begin(), fields.end());
    redis->expire(userKey, std::chrono::seconds(300));

    userConnRes.isSuccess = true;
    tempUser->PushSendMsg(sizeof(userConnRes), (char*)&userConnRes);

    std::cout << "[ProcessLobbyConnect] userId: " << reqPacket->userId << " userPk: " << userPk << '\n';
}

void RedisManager::UserDisConnect(uint16_t connObjNum_) {
    auto tempUser = connUsersManager->FindUser(connObjNum_);

    uint32_t userPk = tempUser->GetPk();
    uint32_t partyId = tempUser->GetPartId();

    if (userPk == 0) return;

    try {
        // 1. 파티 있으면 파티원들에게 오프라인 알림
        if (partyId != 0) {
            NotifyPartyMemberStatus(userPk, partyId, 0);  // 0=오프라인
        }

        // 2. 친구들에게 오프라인 알림
        NotifyFriendOffline(userPk);

        // 3. Redis 상태 처리 (state, equip)
        std::string userKey = "user:" + std::to_string(userPk);
        auto pipe = redis->pipeline();
        pipe.hset(userKey, "state", "offline")
            .expire(userKey, std::chrono::seconds(60))
            .del(userKey + ":equip");
        pipe.exec();

        // 4. pk, objNum 매핑 제거
        connUsersManager->DelPkToObjNum(userPk);
        std::cout << "[UserDisConnect] userPk: " << userPk << '\n';
    }
    catch (const std::exception& e) {
        std::cerr << "[UserDisConnect] Error: " << e.what() << '\n';
    }
}



// ============================================ Pub/Sub 수신 후 타겟 유저에게 전달 ============================================

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
    auto tempConnUser = connUsersManager->FindUserByPk(targetPk_); // 친구 요청 상태 받을 현재 서버에 있는 유저 

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

void RedisManager::SendCostumeChangeToUser(uint32_t targetPk_, uint32_t userPk_, const std::string& userId_, uint8_t slot_, uint32_t itemCode_) {
    COSTUME_CHANGE_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::COSTUME_CHANGE_NOTIFY;
    notify.PacketLength = sizeof(notify);

    auto tempObjNum = connUsersManager->GetObjNumByPk(targetPk_);
    auto temoUser = connUsersManager->FindUser(tempObjNum);

    notify.userPk = userPk_;
    strncpy_s(notify.userId, sizeof(notify.userId), userId_.c_str(), _TRUNCATE);
    notify.slot = slot_;
    notify.itemCode = itemCode_;
    temoUser->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyInfo(uint16_t connObjNum_, uint32_t partyId_) {
    try {
        std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
        std::string leaderKey = "party:" + std::to_string(partyId_) + ":leader";

        // 파티원 pk 목록
        std::unordered_set<std::string> memberPkStrs;
        redis->smembers(membersKey, std::inserter(memberPkStrs, memberPkStrs.begin()));

        // 파티장 pk
        auto leaderPkStr = redis->get(leaderKey);
        uint32_t leaderPk = leaderPkStr.has_value() ? std::stoul(*leaderPkStr) : 0;

        PARTY_INFO_PACKET partyInfo;
        partyInfo.PacketId = (uint16_t)PACKET_ID::PARTY_INFO_PACKET;
        partyInfo.PacketLength = sizeof(PARTY_INFO_PACKET);
        partyInfo.partyId = partyId_;
        partyInfo.leaderPk = leaderPk;
        partyInfo.memberCount = static_cast<uint8_t>(memberPkStrs.size());

        // pipeline으로 파티원 정보 한 번에 조회
        std::vector<uint32_t> memberPks;
        for (const auto& s : memberPkStrs) {
            memberPks.push_back(std::stoul(s));
        }

        auto pipe = redis->pipeline();
        for (auto pk : memberPks) {
            pipe.hget("user:" + std::to_string(pk), "level");
            pipe.hgetall("user:" + std::to_string(pk) + ":equip");
        }
        auto replies = pipe.exec();

        // 파티원 정보 조립
        for (int i = 0; i < (int)memberPks.size(); i++) {
            auto& member = partyInfo.members[i];
            member.userPk = memberPks[i];

            // level
            try {
                auto level = replies.get<sw::redis::OptionalString>(i * 2);
                if (level.has_value()) {
                    member.userLevel = static_cast<uint16_t>(
                        std::stoul(*level));
                }
            }
            catch (...) {}

            // equip (hgetall로 불러오기)
            try {
                std::unordered_map<std::string, std::string> equip;
                replies.get(i * 2 + 1, std::inserter(equip, equip.begin()));
                if (equip.count("head")) member.head = std::stoul(equip["head"]);
                if (equip.count("body")) member.body = std::stoul(equip["body"]);
                if (equip.count("legs")) member.legs = std::stoul(equip["legs"]);
                if (equip.count("feet")) member.feet = std::stoul(equip["feet"]);
            }
            catch (...) {}

            ConnUser* user = connUsersManager->FindUserByPk(memberPks[i]);
            if (user) {
                strncpy_s(member.userId, sizeof(member.userId), user->GetId().c_str(), _TRUNCATE);
            }
        }

        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(partyInfo), (char*)&partyInfo);
    }
    catch (const std::exception& e) {
        std::cerr << "[SendPartyInfo] Error: " << e.what() << '\n';
    }
}

void RedisManager::SendPartyJoinToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, const std::string& userId_, uint16_t userLevel_, uint32_t head_, uint32_t body_, uint32_t legs_, uint32_t feet_) {
    auto tempUser = connUsersManager->FindUserByPk(targetPk_);

    PARTY_JOIN_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_JOIN_NOTIFY;
    notify.PacketLength = sizeof(notify);

    notify.userPk = userPk_;
    strncpy_s(notify.userId, sizeof(notify.userId),
        userId_.c_str(), _TRUNCATE);
    notify.userLevel = userLevel_;
    notify.head = head_;
    notify.body = body_;
    notify.legs = legs_;
    notify.feet = feet_;

    tempUser->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyLeaveToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, uint32_t newLeaderPk_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);

    // newLeaderPk == 0이면 파티 해산하고 세션 초기화
    if (newLeaderPk_ == 0) {
        user->SetPartyId(0);
    }

    PARTY_LEAVE_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_LEAVE_NOTIFY;
    notify.PacketLength = sizeof(notify);
    notify.userPk = userPk_;
    notify.newLeaderPk = newLeaderPk_;  // 0이면 클라도 파티 해산 처리
    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyInviteToUser(uint32_t targetPk_, uint32_t senderPk_, const std::string& senderId_, uint16_t senderLevel_, uint32_t partyId_, uint8_t memberCount_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);

    PARTY_INVITE_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_INVITE_NOTIFY;
    notify.PacketLength = sizeof(notify);
    strncpy_s(notify.senderId, sizeof(notify.senderId),
        senderId_.c_str(), _TRUNCATE);
    notify.senderLevel = senderLevel_;
    notify.partyId = partyId_;
    notify.memberCount = memberCount_;

    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyInviteRejectToUser(uint32_t targetPk_, const std::string& senderId_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);

    PARTY_INVITE_REJECT_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_INVITE_REJECT_NOTIFY;
    notify.PacketLength = sizeof(notify);
    strncpy_s(notify.senderId, sizeof(notify.senderId),senderId_.c_str(), _TRUNCATE);

    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyKickToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t kickedPk_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);
    if (!user) return;

    // 강퇴된 본인이면 세션 초기화
    if (targetPk_ == kickedPk_) {
        user->SetPartyId(0);
    }

    PARTY_KICK_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_KICK_NOTIFY;
    notify.PacketLength = sizeof(notify);
    notify.userPk = kickedPk_;
    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyDelegateToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t newLeaderPk_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);

    PARTY_DELEGATE_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_DELEGATE_NOTIFY;
    notify.PacketLength = sizeof(notify);
    notify.newLeaderPk = newLeaderPk_;
    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendPartyMemberStatusToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, uint8_t onlineStatus_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);
    if (!user) return;

    PARTY_MEMBER_STATUS_NOTIFY notify;
    notify.PacketId = (uint16_t)PACKET_ID::PARTY_MEMBER_STATUS_NOTIFY;
    notify.PacketLength = sizeof(notify);
    notify.userPk = userPk_;
    notify.partyId = partyId_;
    notify.onlineStatus = onlineStatus_;
    user->PushSendMsg(sizeof(notify), (char*)&notify);
}

void RedisManager::SendMatchStartToUser(uint32_t targetPk_) {
    auto user = connUsersManager->FindUserByPk(targetPk_);
    if (!user) return;

    MATCH_START_RESPONSE notify;
    notify.PacketId = (uint16_t)PACKET_ID::MATCH_START_RESPONSE;
    notify.PacketLength = sizeof(notify);
    notify.isSuccess = true;
    user->PushSendMsg(sizeof(notify), (char*)&notify);
}



// ============================================ 내부 헬퍼 ============================================

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


void RedisManager::LeavePartyInternal(uint32_t userPk_, uint32_t partyId_) {
    std::string membersKey = "party:" + std::to_string(partyId_) + ":members";
    std::string leaderKey = "party:" + std::to_string(partyId_) + ":leader";
    std::string userKey = "user:" + std::to_string(userPk_);

    // members에서 제거
    redis->srem(membersKey, std::to_string(userPk_));

    // 세션 + Redis partyId 초기화
    ConnUser* user = connUsersManager->FindUserByPk(userPk_);
    if (user) user->SetPartyId(0);
    redis->hset(userKey, "partyId", "0");

    // 남은 파티원 확인
    auto remainCount = redis->scard(membersKey);

    if (remainCount == 0) { // 파티 해산
        redis->del(membersKey);
        redis->del(leaderKey);
    }
    else if (remainCount == 1) {
        // 2명이었다가 1명 나간 경우 -> 알림 후 해산
        NotifyPartyLeave(userPk_, partyId_, 0);

        std::unordered_set<std::string> remainMembers;
        redis->smembers(membersKey,
            std::inserter(remainMembers, remainMembers.begin()));
        uint32_t lastPk = std::stoul(*remainMembers.begin());

        ConnUser* lastUser = connUsersManager->FindUserByPk(lastPk);
        if (lastUser) lastUser->SetPartyId(0);
        redis->hset("user:" + std::to_string(lastPk), "partyId", "0");

        redis->del(membersKey);
        redis->del(leaderKey);
    }
    else {
        // 3명 이상 -> 파티 유지
        uint32_t newLeaderPk = 0;
        auto leaderPkStr = redis->get(leaderKey);
        if (leaderPkStr.has_value() &&
            std::stoul(*leaderPkStr) == userPk_) {

            std::unordered_set<std::string> remainMembers;
            redis->smembers(membersKey,
                std::inserter(remainMembers, remainMembers.begin()));
            newLeaderPk = std::stoul(*remainMembers.begin());
            redis->set(leaderKey, std::to_string(newLeaderPk));
        }
        NotifyPartyLeave(userPk_, partyId_, newLeaderPk);
    }
}

bool RedisManager::IsPartyLeader(uint32_t userPk_, uint32_t partyId_) {
    try {
        auto leaderPkStr = redis->get("party:" + std::to_string(partyId_) + ":leader");
        if (!leaderPkStr.has_value()) return false;
        return std::stoul(*leaderPkStr) == userPk_;
    }
    catch (const std::exception& e) {
        std::cerr << "[IsPartyLeader] Error: " << e.what() << '\n';
        return false;
    }
}