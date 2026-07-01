#include "LobbyRedisSubscriber.h"

LobbyRedisSubscriber::LobbyRedisSubscriber(int serverId) : serverId_(serverId) {}
LobbyRedisSubscriber::~LobbyRedisSubscriber() {
    Stop();
}

void LobbyRedisSubscriber::Start() {
    running_ = true;
    subThread_ = std::thread(&LobbyRedisSubscriber::SubscribeLoop, this);
    std::cout << "[LobbyRedisSubscriber] Server " << serverId_
        << " subscribing to lobby:events\n";
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

        std::cout << "[LobbyRedisSubscriber] Server " << serverId_ << " subscribing to " << channel << '\n';

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
        case LobbyEventType::FriendOnline:
            HandleFriendOnline(message);
            break;
        case LobbyEventType::FriendOffline:
            HandleFriendOffline(message);
            break;
        case LobbyEventType::CostumeChange:
            HandleCostumeChange(message);
            break;
        case LobbyEventType::FriendAccepted:
            HandleFriendAccepted(message);
            break;
        case LobbyEventType::FriendRemoved:
            HandleFriendRemoved(message);
            break;
        default:
            std::cerr << "[HandleLobbyEvent] Unknown type: " << message << '\n';
            break;
    }
}

void LobbyRedisSubscriber::HandleFriendOnline(const std::string& message) {
    // message 형식 : {"type":1,"data":{"userPk":13}}
    uint32_t userPk = ParseUintField(message, "userPk");
    if (userPk == 0) return;

    
    std::cout << "[HandleFriendOnline] userPk: " << userPk << '\n';
}

void LobbyRedisSubscriber::HandleFriendOffline(const std::string& message) {
    // message 형식 : {"type":2,"data":{"userPk":13}}
    uint32_t userPk = ParseUintField(message, "userPk");
    if (userPk == 0) return;

    std::cout << "[HandleFriendOffline] userPk: " << userPk << '\n';
}

void LobbyRedisSubscriber::HandleCostumeChange(const std::string& message) {
    // message 형식 : {"type":3,"data":{"userPk":13,"slot":1,"itemCode":1024}}
    uint32_t userPk = ParseUintField(message, "userPk");
    uint32_t slot = ParseUintField(message, "slot");
    uint32_t itemCode = ParseUintField(message, "itemCode");

    if (userPk == 0 || slot == 0 || itemCode == 0) return;

    std::cout << "[HandleCostumeChange] userPk: " << userPk << " slot: " << slot << " itemCode: " << itemCode << '\n';
}

void LobbyRedisSubscriber::HandleFriendAccepted(const std::string& message) {
    // message 형식 : {"type":6,"data":{"targetPk":13,"senderPk":5}}
    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    if (targetPk == 0 || senderPk == 0) return;

    std::cout << "[HandleFriendAccepted] targetPk: " << targetPk << " senderPk: " << senderPk << '\n';
}

void LobbyRedisSubscriber::HandleFriendRemoved(const std::string& message) {
    // message 형식 : {"type":7,"data":{"targetPk":13,"senderPk":5}}
    uint32_t targetPk = ParseUintField(message, "targetPk");
    uint32_t senderPk = ParseUintField(message, "senderPk");
    if (targetPk == 0 || senderPk == 0) return;

    std::cout << "[HandleFriendRemoved] targetPk: " << targetPk << " senderPk: " << senderPk << '\n';
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