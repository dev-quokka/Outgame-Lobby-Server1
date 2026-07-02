#pragma once

#include <jwt-cpp/jwt.h>
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "Packet.h"
#include "UserTypes.h"
#include "ConnUsersManager.h"
#include "MySQLManager.h"
#include "ServerChannelEnum.h"

constexpr const char* JWT_SECRET = "quokka_secret_key";
constexpr int MAX_DATA_PACKET_SIZE = 256;

constexpr const char* host = "127.0.0.1";
constexpr int port = 6379;

class RedisManager {
public:
    static RedisManager& GetInstance();
    sw::redis::Redis& GetRedis();

    void SetConnUsersManager(ConnUsersManager* mgr) { connUsersManager = mgr; }

    // ====================== INITIALIZATION =======================
    void Init(const uint16_t RedisThreadCnt_);


    // ===================== PACKET MANAGEMENT =====================
    void PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_);


    // ====================== REDIS =======================
    bool VerifyUserToken(const std::string& userId_, const char* token_, uint32_t& outUserPk_);
    void PublishToUsers(const std::vector<uint32_t>& targetPks_, const std::string& message_);

    void SendFriendRequestToUser(uint32_t targetPk_, uint32_t senderPk_, uint16_t senderLevel_, uint8_t onlineStatus_);
    void NotifyFriendOnline(uint32_t userPk_, const std::vector<uint32_t>& friendPks_);
    void NotifyFriendOffline(uint32_t userPk_);



    // ====================== UserState =======================
    void UserConnect(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    void UserDisConnect(uint16_t connObjNum_);

    void ProcessUserSearch(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    void ProcessFriendRequest(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);

    void ProcessFriendAccept(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_);
    void SendFriendStatusToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t status_);
    void SendFriendAcceptToUser(uint32_t targetPk_, uint32_t friendPk_, uint16_t status_);




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

    // ===================== REDIS MANAGEMENT =====================
    void RedisRun(const uint16_t RedisThreadCnt_);
    bool CreateRedisThread(const uint16_t RedisThreadCnt_);
    void RedisThread();


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