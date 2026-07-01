// 로비 서버가 Redis pub/sub 채널을 구독하여
// 다른 서버에서 발행된 이벤트 메시지를 받아 처리하는 클래스
// 처리 이벤트: 친구 온/오프라인 알림, 친구 수락/삭제 알림, 파티원 코스튬 변경 요청 등

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
    std::vector<uint32_t> ParseTargets(const std::string& message);

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