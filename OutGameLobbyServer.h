#pragma once
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#include "Define.h"
#include "ConnUser.h"
#include "ConnUsersManager.h"
#include "OverLappedManager.h"
#include "ServerChannelEnum.h"
#include "RedisManager.h"
#include "MySQLManager.h"
#include "LobbyHeartbeat.h"
#include "LobbyRedisSubscriber.h"

class OutGameLobbyServer {
public:
    OutGameLobbyServer(uint16_t maxClientCount_) : maxClientCount(maxClientCount_), AcceptQueue(maxClientCount_) {}
    ~OutGameLobbyServer() {}

    // ====================== INITIALIZATION =======================
    bool init();
    bool StartWork();
    void ServerEnd();


private:
    // ===================== THREAD MANAGEMENT =====================
    bool CreateWorkThread();
    bool CreateAccepterThread();
    void WorkThread();
    void AccepterThread();


    boost::lockfree::queue<ConnUser*> AcceptQueue; // For Aceept User Queue

    // 32 bytes
    std::vector<std::thread> workThreads;
    std::vector<std::thread> acceptThreads;

    LobbyHeartbeat  heartbeat_;
    LobbyRedisSubscriber subscriber_; 

    // 8 bytes
    SOCKET serverSkt = INVALID_SOCKET;
    HANDLE sIOCPHandle = INVALID_HANDLE_VALUE;

    OverLappedManager* overLappedManager;
    ConnUsersManager* connUsersManager;

    // 2 bytes
    uint16_t MaxThreadCnt = 0;
    uint16_t maxClientCount = 0;

    // 1 bytes
    std::atomic<bool>  WorkRun = false;
    std::atomic<bool>  AccepterRun = false;
    std::atomic<bool> UserMaxCheck = false;
};
