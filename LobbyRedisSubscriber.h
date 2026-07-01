#pragma once
#include <sw/redis++/redis++.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <string>

#include "RedisManager.h"

class LobbyRedisSubscriber {
public:
    LobbyRedisSubscriber(int serverId);
    ~LobbyRedisSubscriber();

    void Start();
    void Stop();

    void HandleFriendOnline(const std::string& message);
    void HandleFriendOffline(const std::string& message);
    void HandleCostumeChange(const std::string& message);
    void HandleFriendAccepted(const std::string& message);
    void HandleFriendRemoved(const std::string& message);

    uint32_t ParseUintField(const std::string& message, const std::string& key);

    enum class LobbyEventType : uint8_t {
        Unknown = 0,
        FriendOnline = 1,
        FriendOffline = 2,
        CostumeChange = 3,
        PartyUpdate = 4,
        MatchStart = 5,
        FriendAccepted = 6,
        FriendRemoved = 7,
    };

private:

    void SubscribeLoop();
    void HandleLobbyEvent(const std::string& channel, const std::string& message);

    int              serverId_;
    ServerType serverType_;
    std::atomic<bool> running_{ false };
    std::thread      subThread_;
};