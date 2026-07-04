// 로비 서버의 Redis 통신 및 유저 상태 관리 담당 클래스
// - JWT 검증, 세션 관리, 친구/파티/코스튬 이벤트 처리
// - pub/sub 발행 및 수신 후 클라 패킷 전송

#pragma once
#include <jwt-cpp/jwt.h>
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "Packet.h"
#include "JWTConfig.h"
#include "RedisConfig.h"
#include "UserTypes.h"
#include "ConnUsersManager.h"
#include "MySQLManager.h"
#include "ServerChannelEnum.h"

class RedisManager {
public:
    static RedisManager& GetInstance();
    sw::redis::Redis& GetRedis();

    void SetConnUsersManager(ConnUsersManager* mgr) { connUsersManager = mgr; }


    // ===================== REDIS MANAGEMENT =====================
    void RedisRun(const uint16_t RedisThreadCnt_);
    bool CreateRedisThread(const uint16_t RedisThreadCnt_);
    void RedisThread();


    // ====================== INITIALIZATION =======================
    void Init(const uint16_t RedisThreadCnt_);


    // ===================== PACKET MANAGEMENT =====================
    void PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_);


    // ====================== REDIS =======================
    bool VerifyUserToken(const std::string& userId_, const char* token_, uint32_t& outUserPk_); // JWT 토큰 검증 + userPk 추출
   

    // ====================== 유저 상태 ======================
    
    // 로비 서버 접속 처리 (JWT 검증, 세션/Redis 세팅, 친구 온라인 알림)
    void UserConnect(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 로비 서버 연결 종료 처리 (세션 정리, 친구 오프라인 알림)
    void UserDisConnect(uint16_t connObjNum_);



    // ====================== 클라 요청 처리 ======================
    // *********** 해당 서버에 접속한 유저의 요청 처리 ************

    // 유저 ID로 유저 검색 + 온라인 상태 반환
    void ProcessUserSearch(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 친구 요청 전송 (DB INSERT + 상대 온라인이면 pub/sub 알림)
    void ProcessFriendRequest(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 친구 요청 수락/거절 (DB UPDATE or DELETE + pub/sub 알림)
    void ProcessFriendAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 코스튬 변경 (인벤 확인 + DB UPDATE + Redis 갱신 + 파티원 pub/sub 알림)
    void ProcessCostumeChange(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티 따라가기 (대상 파티 없으면 생성, 있으면 입장)
    void ProcessPartyFollow(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티 초대 전송 (상대 온라인이면 pub/sub 알림)
    void ProcessPartyInvite(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티 초대 수락/거절 처리
    void ProcessPartyInviteAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티 탈퇴 (2명이면 해산, 3명 이상이면 유지 + 파티장이면 자동 위임)
    void ProcessPartyLeave(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티원 강퇴 (파티장만 가능)
    void ProcessPartyKick(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 파티장 위임 (파티장만 가능)
    void ProcessPartyDelegate(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    // 매칭 시작 (파티 없으면 혼자, 파티 있으면 파티장만 가능)
    void ProcessMatchStart(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);



    // ====================== REDIS Pub/Sub 발행 ======================
    // *********** 친구들이 있는 서버 채널로 이벤트 publish ***********

    // 해당 유저 접속 시 친구들에게 온라인 알림
    void NotifyFriendOnline(uint32_t userPk_, const std::vector<uint32_t>& friendPks_);
    // 해당 유저 로그아웃 시 친구들에게 오프라인 알림 (DB 조회 후 publish)
    void NotifyFriendOffline(uint32_t userPk_);
    // 코스튬 변경 시 파티원들에게 변경 알림
    void NotifyCostumeChangeToParty(uint32_t userPk_, const std::string& userId_, uint32_t partyId_, uint8_t slot_, uint32_t itemCode_);
    // 새 파티원 입장 시 기존 파티원들에게 알림
    void NotifyPartyJoin(uint32_t newUserPk_, uint32_t partyId_);
    // 파티원 탈퇴 시 남은 파티원들에게 알림 (newLeaderPk=0이면 파티 해산)
    void NotifyPartyLeave(uint32_t userPk_, uint32_t partyId_, uint32_t newLeaderPk_);
    // 파티원 강퇴 시 강퇴된 유저 + 남은 파티원들에게 알림
    void NotifyPartyKick(uint32_t targetPk_, uint32_t partyId_);
    // 파티장 위임 시 파티원들에게 새 파티장 알림
    void NotifyPartyDelegate(uint32_t newLeaderPk_, uint32_t partyId_);
    // 파티원 온라인/오프라인 상태 변경 알림 (팅김/재접속 시)
    void NotifyPartyMemberStatus(uint32_t userPk_, uint32_t partyId_, uint8_t onlineStatus_);
    // 매칭 시작 시 파티원들에게 알림
    void NotifyMatchStart(uint32_t leaderPk_, uint32_t partyId_);



    // ====================== Pub/Sub 수신 후 타겟 유저에게 전달 ======================
    // *** 다른 서버에서 publish된 메시지를 받아 이 서버의 타겟 유저에게 패킷 전송 ****

    // 친구 온라인/오프라인 상태 변경 패킷 전송
    void SendFriendStatusToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t status_);
    // 친구 요청 수락/거절 결과 패킷 전송
    void SendFriendAcceptToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t status_);
    // 친구 요청 알림 패킷 전송 (요청자 정보 포함)
    void SendFriendRequestToUser(uint32_t targetPk_, uint32_t senderPk_, const std::string& senderId_, uint16_t senderLevel_, uint8_t onlineStatus_);
    // 파티원 코스튬 변경 알림 패킷 전송
    void SendCostumeChangeToUser(uint32_t targetPk_, uint32_t userPk_, const std::string& userId_, uint8_t slot_, uint32_t itemCode_);
    // 파티 전체 정보 패킷 전송 (파티 입장 시 사용)
    void SendPartyInfo(uint16_t connObjNum_, uint32_t partyId_);
    // 새 파티원 입장 알림 패킷 전송 (코스튬 정보 포함)
    void SendPartyJoinToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, const std::string& userId_, uint16_t userLevel_, uint32_t head_, uint32_t body_, uint32_t legs_, uint32_t feet_);
    // 파티원 탈퇴 알림 패킷 전송 (newLeaderPk=0이면 파티 해산)
    void SendPartyLeaveToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, uint32_t newLeaderPk_);
    // 파티 초대 알림 패킷 전송
    void SendPartyInviteToUser(uint32_t targetPk_, uint32_t senderPk_, const std::string& senderId_, uint16_t senderLevel_, uint32_t partyId_, uint8_t memberCount_);
    // 파티 초대 거절 알림 패킷 전송 (초대한 유저에게)
    void SendPartyInviteRejectToUser(uint32_t targetPk_, const std::string& senderId_);
    // 파티원 강퇴 알림 패킷 전송 (강퇴된 유저 + 파티원들에게)
    void SendPartyKickToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t kickedPk_);
    // 파티장 위임 알림 패킷 전송 (파티원들에게)
    void SendPartyDelegateToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t newLeaderPk_);
    // 파티원 온라인/오프라인 상태 알림 패킷 전송
    void SendPartyMemberStatusToUser(uint32_t targetPk_, uint32_t partyId_, uint32_t userPk_, uint8_t onlineStatus_);
    // 매칭 시작 알림 패킷 전송 (파티원들에게)
    void SendMatchStartToUser(uint32_t targetPk_);



    // ====================== 내부 헬퍼 ======================

    // 타겟 pk 목록의 서버 위치 조회 후 서버별로 묶어 publish
    void PublishToUsers(const std::vector<uint32_t>& targetPks_, const std::string& message_);
    // 파티 탈퇴 공통 로직
    void LeavePartyInternal(uint32_t userPk_, uint32_t partyId_);
    // Redis에서 파티장 여부 확인 (파티장 전용 기능 권한 체크용)
    bool IsPartyLeader(uint32_t userPk_, uint32_t partyId_);

    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

private:
    RedisManager() = default;
    ~RedisManager() {
        redisRun = false;
        for (int i = 0; i < redisThreads.size(); i++) { // Shutdown Redis Threads
            if (redisThreads[i].joinable()) {
                redisThreads[i].join();
            }
        }
    }


    typedef void(RedisManager::* RECV_PACKET_FUNCTION)(uint16_t, uint16_t, char*);

    // 242 bytes
    sw::redis::ConnectionOptions connection_options;

    // 136 bytes
    boost::lockfree::queue<DataPacket> procSktQueue{ MAX_DATA_PACKET_SIZE };

    // 80 bytes
    std::unordered_map<uint16_t, RECV_PACKET_FUNCTION> packetIDTable;
    std::unordered_map<std::string, std::vector<uint16_t>> missionMap;

    // 40 bytes
    std::string buyItemSha;

    // 32 bytes
    std::vector<std::thread> redisThreads;

    // 8 bytes
    std::unique_ptr<sw::redis::Redis> redis;
    ConnUsersManager* connUsersManager;

    // 1 bytes
    bool redisRun = false;
};