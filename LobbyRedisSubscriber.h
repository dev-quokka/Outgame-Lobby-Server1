// 로비 서버가 Redis pub/sub 채널을 구독하여
// 다른 서버에서 발행된 이벤트 메시지를 받아 처리하는 클래스
// 처리 이벤트: 친구 온/오프라인 알림, 친구 수락/삭제 알림, 파티원 코스튬 변경 요청 등

#pragma once
#include <sw/redis++/redis++.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <string>

#include "ServerChannelEnum.h"

class LobbyRedisSubscriber {
public:
    LobbyRedisSubscriber();
    ~LobbyRedisSubscriber();

    void Start(int serverId_);
    void Stop();

    // ====================== 친구 이벤트 ======================
    void HandleFriendOnline(const std::string& message);    // 친구 접속 알림 처리
    void HandleFriendOffline(const std::string& message);   // 친구 로그아웃 알림 처리
    void HandleFriendRequest(const std::string& message);   // 친구 요청 알림 처리
    void HandleFriendAccepted(const std::string& message);  // 친구 수락/거절 알림 처리
    void HandleFriendRemoved(const std::string& message);   // 친구 삭제 알림 처리


    // ====================== 코스튬 이벤트 ======================
    void HandleCostumeChange(const std::string& message);   // 파티원 코스튬 변경 알림 처리


    // ====================== 파티 이벤트 ======================
    void HandlePartyInvite(const std::string& message);       // 파티 초대 알림 처리
    void HandlePartyJoin(const std::string& message);         // 파티원 입장 알림 처리
    void HandlePartyLeave(const std::string& message);        // 파티원 탈퇴 알림 처리
    void HandlePartyKick(const std::string& message);         // 파티원 강퇴 알림 처리
    void HandlePartyDelegate(const std::string& message);     // 파티장 위임 알림 처리
    void HandlePartyMemberStatus(const std::string& message); // 파티원 온라인/오프라인 상태 알림 처리
    void HandleMatchStart(const std::string& message);        // 매칭 시작


    // ====================== 파싱 헬퍼 ======================
    // message에서 특정 키의 '정수값' 추출
    // {"type":1,"data":{"userPk":13}} 에서 "userPk" -> 13
    uint32_t ParseUintField(const std::string& message, const std::string& key);

    // message에서 targets '배열' 추출
    // {"type":1,"data":{"userPk":13,"targets":[14,15]}} 에서 {14, 15} 추출
    std::vector<uint32_t> ParseTargets(const std::string& message);
    std::string ParseStringField(const std::string& message, const std::string& key);

    enum class LobbyEventType : uint8_t {
        Unknown = 0,

        // 친구 관련
        FriendOnline = 1,   // 친구 접속
        FriendOffline = 2,   // 친구 로그아웃
        CostumeChange = 3,   // 파티원 코스튬 변경
        FriendAccepted = 6,   // 친구 요청 수락/거절
        FriendRemoved = 7,   // 친구 삭제
        FriendRequest = 8,   // 친구 요청

        // 파티 관련
        PartyInvite = 9,   // 파티 초대
        PartyJoin = 10,  // 파티원 입장 (재접속 포함)
        PartyLeave = 11,  // 파티원 탈퇴
        PartyKick = 12,  // 파티원 강퇴
        PartyDelegate = 13,  // 파티장 위임
        PartyMemberStatus = 14,  // 파티원 온라인/오프라인 상태 변경

        // 매칭 관련
        MatchStart = 5,  // 매칭 시작
    };

private:

    void SubscribeLoop();
    void HandleLobbyEvent(const std::string& channel, const std::string& message);

    int              serverId;
    ServerType serverType_;
    std::atomic<bool> running_{ false };
    std::thread      subThread_;
};