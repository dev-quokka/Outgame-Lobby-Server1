\#pragma once
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "Packet.h"
#include "LobbyHeartbeat.h"
#include "ConnUsersManager.h"

class RedisManager {
public:
    static RedisManager& GetInstance();
    sw::redis::Redis& GetRedis();


    // ====================== INITIALIZATION =======================
    void Connect(const std::string& host, int port);
    void Init(const uint16_t RedisThreadCnt_);


    // ===================== PACKET MANAGEMENT =====================
    void PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_);


    // ==================== CONNECTION INTERFACE ===================
    void Disconnect(uint16_t connObjNum_);


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
    boost::lockfree::queue<DataPacket> procSktQueue{ MAX_CENTER_PACKET_SIZE };

    // 80 bytes
    std::unordered_map<uint16_t, RECV_PACKET_FUNCTION> packetIDTable;
    std::unordered_map<std::string, std::vector<uint16_t>> missionMap;

    // 40 bytes
    std::string buyItemSha;

    // 32 bytes
    std::vector<std::thread> redisThreads;

    // 8 bytes
    std::unique_ptr<sw::redis::Redis> redis;
    std::unique_ptr<LobbyHeartbeat> lobbyHeartbeat;

    ConnUsersManager* connUsersManager;

    // 1 bytes
    bool redisRun = false;
};