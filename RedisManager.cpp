#include "RedisManager.h"

RedisManager& RedisManager::GetInstance() {
    static RedisManager instance;  // 싱글톤 초기화
    return instance;
}

void RedisManager::Connect(const std::string& host, int port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    opts.socket_timeout = std::chrono::seconds(10);
    opts.keep_alive = true;

    try {
        redis = std::make_unique<sw::redis::Redis>(opts);
        redis->ping();
        std::cout << "[Redis] Connected to " << host << ":" << port << std::endl;
    }
    catch (const sw::redis::Error& e) {
        std::cerr << "[Redis] Connection failed: " << e.what() << std::endl;
        throw;  // 연결 실패하면 서버 시작 중단
    }
}

sw::redis::Redis& RedisManager::GetRedis() {
    if (!redis) {
        throw std::runtime_error("[Redis] Not connected. Call Connect() first.");
    }
    return *redis;
}


void RedisManager::Init(const uint16_t RedisThreadCnt_) {

    // -------------------- SET PACKET HANDLERS ----------------------
    packetIDTable = std::unordered_map<uint16_t, RECV_PACKET_FUNCTION>();

    // LOGIN


    RedisRun(RedisThreadCnt_);

    lobbyHeartbeat = std::make_unique<LobbyHeartbeat>();
    lobbyHeartbeat->Start();
}

// ===================== PACKET MANAGEMENT =====================

void RedisManager::PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_) {
    ConnUser* TempConnUser = connUsersManager->FindUser(connObjNum_);
    TempConnUser->WriteRecvData(recvData_, size_); // Push Data in Circualr Buffer
    DataPacket tempD(size_, connObjNum_);
    procSktQueue.push(tempD);
}


// ==================== CONNECTION INTERFACE ===================

void RedisManager::Disconnect(uint16_t connObjNum_) {
    if (connUsersManager->FindUser(connObjNum_)->GetPk() == 0) return; // Check the server closed

    auto tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);
    auto tempPk = tempUser->GetPk();

    std::string tag = "{" + std::to_string(tempPk) + "}";
    std::string userInfokey = "userinfo:" + tag;
    std::string equipmentkey = "equipment:" + tag;
    std::string consumablekey = "consumables:" + tag;
    std::string materialkey = "materials:" + tag;

    try {
        auto pipe = redis->pipeline(tag);

        redis->hset(userInfokey, "userstate", "offline"); // Set user status to "offline" in Redis Cluster
        redis->expire(equipmentkey, std::chrono::seconds(180)); // ttl 3분 설정
        redis->expire(consumablekey, std::chrono::seconds(180)); // ttl 3분 설정
        redis->expire(materialkey, std::chrono::seconds(180)); // ttl 3분 설정

        pipe.exec();
    }
    catch (const sw::redis::Error& e) {
        std::cerr << "Redis error : " << e.what() << std::endl;
        return;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception error : " << e.what() << std::endl;
    }

}


// ====================== REDIS MANAGEMENT =====================

void RedisManager::RedisRun(const uint16_t RedisThreadCnt_) { // Connect Redis Server

    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    opts.socket_timeout = std::chrono::seconds(10);
    opts.keep_alive = true;

    try {
        redis = std::make_unique<sw::redis::Redis>(opts);
        redis->ping();
        std::cout << "[Redis] Connected to " << host << ":" << port << std::endl;
    }
    catch (const sw::redis::Error& e) {
        std::cerr << "[Redis] Connection failed: " << e.what() << std::endl;
        throw;  // 연결 실패하면 서버 시작 중단
    }

    try {
        connection_options.host = "127.0.0.1";  // Redis Cluster IP
        connection_options.port = 7001;  // Redis Cluster Master Node Port
        connection_options.socket_timeout = std::chrono::seconds(10);
        connection_options.keep_alive = true;

        redis = std::make_unique<sw::redis::RedisCluster>(connection_options);
        std::cout << "Redis Cluster Connected" << std::endl;

        CreateRedisThread(RedisThreadCnt_);

    }
    catch (const  sw::redis::Error& err) {
        std::cout << "Redis Connect Error : " << err.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception error : " << e.what() << std::endl;
    }
}

bool RedisManager::CreateRedisThread(const uint16_t RedisThreadCnt_) {
    redisRun = true;

    try {
        for (int i = 0; i < RedisThreadCnt_; i++) {
            redisThreads.emplace_back(std::thread([this]() { RedisThread(); }));
        }
    }
    catch (const std::system_error& e) {
        std::cerr << "Create Redis Thread Failed : " << e.what() << std::endl;
        return false;
    }

    return true;
}

void RedisManager::RedisThread() {
    DataPacket tempD(0, 0);
    ConnUser* TempConnUser = nullptr;
    char tempData[1024] = { 0 };

    while (redisRun) {
        if (procSktQueue.pop(tempD)) {
            std::memset(tempData, 0, sizeof(tempData));
            TempConnUser = connUsersManager->FindUser(tempD.connObjNum); // Find User
            PacketInfo packetInfo = TempConnUser->ReadRecvData(tempData, tempD.dataSize); // GetData
            (this->*packetIDTable[packetInfo.packetId])(packetInfo.connObjNum, packetInfo.dataSize, packetInfo.pData); // Proccess Packet
        }
        else { // Empty Queue
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}