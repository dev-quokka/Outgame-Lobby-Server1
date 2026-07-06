#include "LobbyHeartbeat.h"

LobbyHeartbeat::LobbyHeartbeat() {}
LobbyHeartbeat::~LobbyHeartbeat() {
    Stop();
}

void LobbyHeartbeat::Start(int serverId_) {
    serverId = serverId_;
    running = true;
    heartbeatThread = std::thread(&LobbyHeartbeat::HeartbeatLoop, this);
}

void LobbyHeartbeat::Stop() {
    running = false;
    if (heartbeatThread.joinable())
        heartbeatThread.join();
}

void LobbyHeartbeat::OnUserConnected() { ++userCount; }
void LobbyHeartbeat::OnUserDisconnected() { --userCount; }

void LobbyHeartbeat::HeartbeatLoop() {
    auto& redis = RedisManager::GetInstance().GetRedis();

    // 키: lobby:1:status, lobby:2:status
    std::string key = "lobby:" + std::to_string(serverId) + ":status";

    // 서버 시작 직후 첫 heartbeat 바로 전송
    while (running) {
        try {
            int count = userCount.load();
            
            redis.set(key,
                std::to_string(count),
                std::chrono::seconds(TtlSec));
        }
        catch (const std::exception& e) { // Redis 일시 장애 시 로그만 남기고 다음 주기에 재시도
            std::cerr << "[Heartbeat] Redis set failed (server " << serverId << "): " << e.what() << '\n';
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(IntervalSec));
    }

    // 서버 정상 종료 시 키 직접 삭제해서 배정 대상에서 제외 (무중단 시스템 도입 시도)
    try {
        redis.del(key);
        std::cout << "[Heartbeat] Server " << serverId << " removed from lobby pool\n";
    }
    catch (...) {
    
    }
}