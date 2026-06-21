#pragma once

#include <atomic>
#include <thread>
#include <chrono>

#include "RedisConnection.h"

class LobbyServer {
public:
    void Start() {
        running_ = true;
        heartbeatThread_ = std::thread(&LobbyServer::HeartbeatLoop, this);
    }

    void Stop() {
        running_ = false;
        if (heartbeatThread_.joinable())
            heartbeatThread_.join();   // 종료 시 깔끔하게 정리
    }

    // 워커 스레드들이 호출 - 유저 접속/해제 시
    void OnUserConnected() { ++userCount_; }
    void OnUserDisconnected() { --userCount_; }

private:
    void HeartbeatLoop() {
        //auto& redis = RedisConnection::GetInstance().GetRedis();
        while (running_) {
            int count = userCount_.load();   // atomic 읽기, 락 불필요
            try {
                redis.set("lobby:1:status",
                    std::to_string(count),
                    std::chrono::seconds(15));   // TTL 15초
            }
            catch (const std::exception& e) {
                std::cerr << "[Heartbeat] Redis set failed: " << e.what() << '\n';
                // 실패해도 루프는 계속 - 다음 주기에 재시도
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::atomic<bool> running_{ false };
    std::atomic<int> userCount_{ 0 };
    std::thread heartbeatThread_;
};