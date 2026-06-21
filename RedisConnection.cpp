#include "RedisConnection.h"
#include <iostream>
#include <stdexcept>

RedisConnection& RedisConnection::GetInstance() {
    static RedisConnection instance;  // 싱글톤 초기화
    return instance;
}

void RedisConnection::Connect(const std::string& host, int port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    opts.socket_timeout = std::chrono::seconds(10);
    opts.keep_alive = true;

    try {
        redis_ = std::make_unique<sw::redis::Redis>(opts);
        redis_->ping();
        std::cout << "[Redis] Connected to " << host << ":" << port << std::endl;
    }
    catch (const sw::redis::Error& e) {
        std::cerr << "[Redis] Connection failed: " << e.what() << std::endl;
        throw;  // 연결 실패하면 서버 시작 중단
    }
}

sw::redis::Redis& RedisConnection::GetRedis() {
    if (!redis_) {
        throw std::runtime_error("[Redis] Not connected. Call Connect() first.");
    }
    return *redis_;
}