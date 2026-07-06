#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <iostream>

#include "RedisManager.h"

constexpr int IntervalSec = 5;
constexpr int TtlSec = 15;

class LobbyHeartbeat {
public:
    LobbyHeartbeat();
    ~LobbyHeartbeat();

    void Start(int serverId_);
    void Stop();

    void OnUserConnected();
    void OnUserDisconnected();

    int GetUserCount() { return userCount.load(); }

private:
    void HeartbeatLoop();

    std::thread heartbeatThread;

    int serverId;

    std::atomic<bool> running{ false };
    std::atomic<int> userCount{ 0 };
};