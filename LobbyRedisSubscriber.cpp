#include "LobbyRedisSubscriber.h"
#include "RedisManager.h"

LobbyRedisSubscriber::LobbyRedisSubscriber() {}
LobbyRedisSubscriber::~LobbyRedisSubscriber() {
    Stop();
}

void LobbyRedisSubscriber::Start(int serverId_) {
    serverId = serverId_;
    serverType_ = SERVER_TYPE;

    running_ = true;
    subThread_ = std::thread(&LobbyRedisSubscriber::SubscribeLoop, this);
    std::cout << "[LobbyRedisSubscriber] Server " << serverId_ << " subscribing to lobby:events\n";
}

void LobbyRedisSubscriber::Stop() {
    running_ = false;
    if (subThread_.joinable())
        subThread_.join();
}

void LobbyRedisSubscriber::SubscribeLoop() {
    auto& redis = RedisManager::GetInstance().GetRedis();

    // 자기 서버 이름으로 채널 구독
    std::string channel = GetServerName(serverType_) + ":events";

    try {
        auto sub = redis.subscriber();

        sub.on_message([this](std::string channel, std::string message) {
            HandleLobbyEvent(channel, message);
            });

        sub.subscribe(channel);

        std::cout << "[LobbyRedisSubscriber] Server " << serverId << " subscribing to " << channel << '\n';

        while (running_) {
            try {
                sub.consume();
            }
            catch (const sw::redis::TimeoutError&) {
                continue;
            }
            catch (const sw::redis::Error& e) {
                std::cerr << "[LobbyRedisSubscriber] Redis error: " << e.what() << '\n';
                break;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[LobbyRedisSubscriber] Exception: " << e.what() << '\n';
    }
}

void LobbyRedisSubscriber::HandleLobbyEvent(const std::string& channel, const std::string& message) {

    auto pos = message.find("\"type\":");
    if (pos == std::string::npos) {
        std::cerr << "[HandleLobbyEvent] Invalid message: " << message << '\n';
        return;
    }
    pos += 7;
    LobbyEventType type = static_cast<LobbyEventType>(std::stoi(message.substr(pos)));

    switch (type) {
        // 친구 관련
    case LobbyEventType::FriendOnline:
        HandleFriendOnline(message);
        break;
    case LobbyEventType::FriendOffline:
        HandleFriendOffline(message);
        break;
    case LobbyEventType::FriendAccepted:
        HandleFriendAccepted(message);
        break;
    case LobbyEventType::FriendRemoved:
        HandleFriendRemoved(message);
        break;
    case LobbyEventType::FriendRequest:
        HandleFriendRequest(message);
        break;

        // 코스튬 관련
    case LobbyEventType::CostumeChange:
        HandleCostumeChange(message);
        break;

        // 파티 관련
    case LobbyEventType::PartyInvite:
        HandlePartyInvite(message);
        break;
    case LobbyEventType::PartyJoin:
        HandlePartyJoin(message);
        break;
    case LobbyEventType::PartyLeave:
        HandlePartyLeave(message);
        break;
    case LobbyEventType::PartyKick:
        HandlePartyKick(message);
        break;
    case LobbyEventType::PartyDelegate:
        HandlePartyDelegate(message);
        break;
    case LobbyEventType::PartyMemberStatus:
        HandlePartyMemberStatus(message);
        break;

        // 매칭 관련
    case LobbyEventType::MatchStart:
        HandleMatchStart(message);
        break;

    default:
        std::cerr << "[HandleLobbyEvent] Unknown type: " << message << '\n';
        break;
    }
}



void LobbyRedisSubscriber::HandleFriendOnline(const std::string& message) {
    uint32_t userPk = ParseUintField(message, "userPk");
    if (userPk == 0) return;

    // targets 파싱
    auto targets = ParseTargets(message);  // [userPk, targetPk]
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendFriendStatusToUser(targetPk, userPk, 1);  // targetPk에게 userPk의 온라인 메시지 전달
    }
}

void LobbyRedisSubscriber::HandleFriendOffline(const std::string& message) {
    uint32_t userPk = ParseUintField(message, "userPk");
    if (userPk == 0) return;

    // targets 파싱
    auto targets = ParseTargets(message);  // [userPk, targetPk]
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendFriendStatusToUser(targetPk, userPk, 0);  // targetPk에게 userPk의 오프라인 메시지 전달
    }
}



void LobbyRedisSubscriber::HandleFriendRequest(const std::string& message) {
    // message 형식: {"type":8,"data":{"targetPk":14,"senderPk":5,"senderId":"dongchan","senderLevel":30,"onlineStatus":1}}

    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    std::string senderId = ParseStringField(message, "senderId");
    uint32_t senderLevel = ParseUintField(message, "senderLevel");
    uint32_t onlineStatus = ParseUintField(message, "onlineStatus");
    if (targetPk == 0 || senderPk == 0 || senderId.empty()) return;

    RedisManager::GetInstance().SendFriendRequestToUser(targetPk, senderPk, senderId,
        static_cast<uint16_t>(senderLevel), static_cast<uint8_t>(onlineStatus));
}

void LobbyRedisSubscriber::HandleFriendAccepted(const std::string& message) {
    // message 형식 : {"type":6,"data":{"targetPk":13,"senderPk":5}}

    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    if (targetPk == 0 || senderPk == 0) return;

    RedisManager::GetInstance().SendFriendAcceptToUser(targetPk, senderPk, 0);  // targetPk에게 senderPk의 친구 수락 메시지 전달
    std::cout << "[HandleFriendAccepted] targetPk: " << targetPk << " senderPk: " << senderPk << '\n';
}

void LobbyRedisSubscriber::HandleFriendRemoved(const std::string& message) {
    // message 형식 : {"type":7,"data":{"targetPk":13,"senderPk":5}}

    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    if (targetPk == 0 || senderPk == 0) return;

    RedisManager::GetInstance().SendFriendAcceptToUser(targetPk, senderPk, 1);  // targetPk에게 senderPk의 친구 수락 메시지 전달
    std::cout << "[HandleFriendRemoved] targetPk: " << targetPk << " senderPk: " << senderPk << '\n';
}



void LobbyRedisSubscriber::HandleCostumeChange(const std::string& message) {
    // {"type":3,"data":{"userPk":13,"userId":"dongchan","slot":1,"itemCode":1024,"targets":[14,15]}}
    uint32_t userPk = ParseUintField(message, "userPk");
    std::string userId = ParseStringField(message, "userId");
    uint32_t slot = ParseUintField(message, "slot");
    uint32_t itemCode = ParseUintField(message, "itemCode");
    if (userPk == 0 || userId.empty()) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendCostumeChangeToUser(targetPk, userPk, userId,static_cast<uint8_t>(slot), itemCode);
    }
}

void LobbyRedisSubscriber::HandlePartyJoin(const std::string& message) {
    // {"type":10,"data":{"partyId":7,"userPk":13,"userId":"dongchan",
    //  "userLevel":30,"head":1024,"body":2048,"legs":3072,"feet":4096,
    //  "targets":[14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t userPk = ParseUintField(message, "userPk");
    std::string userId = ParseStringField(message, "userId");
    uint32_t userLevel = ParseUintField(message, "userLevel");
    uint32_t head = ParseUintField(message, "head");
    uint32_t body = ParseUintField(message, "body");
    uint32_t legs = ParseUintField(message, "legs");
    uint32_t feet = ParseUintField(message, "feet");
    if (partyId == 0 || userPk == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendPartyJoinToUser(
            targetPk, partyId, userPk, userId,
            static_cast<uint16_t>(userLevel),
            head, body, legs, feet);
    }
}

void LobbyRedisSubscriber::HandlePartyLeave(const std::string& message) {
    // {"type":11,"data":{"partyId":7,"userPk":13,"newLeaderPk":14,
    //  "targets":[14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t userPk = ParseUintField(message, "userPk");
    uint32_t newLeaderPk = ParseUintField(message, "newLeaderPk");
    if (partyId == 0 || userPk == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendPartyLeaveToUser(targetPk, partyId, userPk, newLeaderPk);
    }
}

void LobbyRedisSubscriber::HandlePartyInvite(const std::string& message) {
    // reject=1이면 거절 처리
    uint32_t reject = ParseUintField(message, "reject");
    if (reject == 1) {
        uint32_t targetPk = ParseUintField(message, "targetPk");
        std::string senderId = ParseStringField(message, "senderId");
        if (targetPk == 0) return;

        auto targets = ParseTargets(message);
        for (auto pk : targets) {
            RedisManager::GetInstance().SendPartyInviteRejectToUser(
                pk, senderId);
        }
        return;
    }
    
    // 초대 알림 처리
    // {"type":9,"data":{"targetPk":14,"senderPk":5,"senderId":"dongchan",
    //  "senderLevel":30,"partyId":7,"memberCount":2,"targets":[14]}}
    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    std::string senderId = ParseStringField(message, "senderId");
    uint32_t senderLevel = ParseUintField(message, "senderLevel");
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t memberCount = ParseUintField(message, "memberCount");
    if (targetPk == 0 || senderPk == 0) return;

    RedisManager::GetInstance().SendPartyInviteToUser(targetPk, senderPk, senderId, static_cast<uint16_t>(senderLevel), partyId, static_cast<uint8_t>(memberCount));
}

void LobbyRedisSubscriber::HandlePartyKick(const std::string& message) {
    // {"type":12,"data":{"partyId":7,"userPk":13,"targets":[13,14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t userPk = ParseUintField(message, "userPk");
    if (partyId == 0 || userPk == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendPartyKickToUser(
            targetPk, partyId, userPk);
    }
}

void LobbyRedisSubscriber::HandlePartyDelegate(const std::string& message) {
    // {"type":13,"data":{"partyId":7,"newLeaderPk":14,"targets":[5,14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t newLeaderPk = ParseUintField(message, "newLeaderPk");
    if (partyId == 0 || newLeaderPk == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendPartyDelegateToUser(targetPk, partyId, newLeaderPk);
    }
}

void LobbyRedisSubscriber::HandlePartyMemberStatus(const std::string& message) {
    // {"type":14,"data":{"partyId":7,"userPk":13,"onlineStatus":0,"targets":[14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    uint32_t userPk = ParseUintField(message, "userPk");
    uint32_t onlineStatus = ParseUintField(message, "onlineStatus");
    if (partyId == 0 || userPk == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendPartyMemberStatusToUser(targetPk, partyId, userPk, static_cast<uint8_t>(onlineStatus));
    }
}

void LobbyRedisSubscriber::HandleMatchStart(const std::string& message) {
    // {"type":5,"data":{"partyId":7,"targets":[14,15]}}
    uint32_t partyId = ParseUintField(message, "partyId");
    if (partyId == 0) return;

    auto targets = ParseTargets(message);
    for (auto targetPk : targets) {
        RedisManager::GetInstance().SendMatchStartToUser(targetPk);
    }
}


// message에서 특정 키의 정수값 추출하는 함수
// {"type":1,"data":{"userPk":13}}에서 13을 추출
uint32_t LobbyRedisSubscriber::ParseUintField(const std::string& message, const std::string& key) {
    std::string search = "\"" + key + "\":";
    
    auto pos = message.find(search);
    if (pos == std::string::npos) return 0;
    
    pos += search.length();
    return static_cast<uint32_t>(std::stoul(message.substr(pos)));
}

// message에서 targets 배열을 추출하는 함수
// {"type":1,"data":{"userPk":13,"targets":[14,15......]}}에서 {14, 15.....} 추출
std::vector<uint32_t> LobbyRedisSubscriber::ParseTargets(const std::string& message) {
    std::vector<uint32_t> targets;
    auto pos = message.find("\"targets\":[");
    if (pos == std::string::npos) return targets;

    pos += 11;  // "targets":[ 길이
    auto end = message.find("]", pos);
    if (end == std::string::npos) return targets;

    std::string arr = message.substr(pos, end - pos);
    std::stringstream ss(arr);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            targets.push_back(std::stoul(token));
        }
    }
    return targets;
}

// 문자열 필드 추출하는 함수
std::string LobbyRedisSubscriber::ParseStringField(const std::string& message, const std::string& key) {
    std::string search = "\"" + key + "\":\"";

    auto pos = message.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    
    auto end = message.find("\"", pos);
    
    if (end == std::string::npos) return "";
    return message.substr(pos, end - pos);
}