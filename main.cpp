#include <iostream>
#include "RedisManager.h"

int main() {
    RedisManager::GetInstance().Connect("127.0.0.1", 6379); // 레디스 연결
    auto& redis = RedisManager::GetInstance().GetRedis();

    return 0;
}