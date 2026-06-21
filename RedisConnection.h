#pragma once

#include <sw/redis++/redis++.h>
#include <memory>
#include <string>

class RedisConnection {
public:
    static RedisConnection& GetInstance();

    void Connect(const std::string& host, int port);

    sw::redis::Redis& GetRedis();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

private:
    RedisConnection() = default;
    ~RedisConnection() = default;

    std::unique_ptr<sw::redis::Redis> redis_;
};