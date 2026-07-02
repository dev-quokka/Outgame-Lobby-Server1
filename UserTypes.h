#pragma once
#include <cstdint>
#include <string>

constexpr uint16_t MAX_USER_ID_LEN = 21;
constexpr uint16_t MAX_FRIEND_SIZE = 50; // 최대 친구 수
constexpr uint16_t MAX_INVENTORY_SIZE = 100; // 최대 아이템 수
constexpr uint16_t MAX_USER_PASSWORD_LEN = 256;

struct Costume {
    uint32_t head = 0;
    uint32_t body = 0;
    uint32_t legs = 0;
    uint32_t feet = 0;
};

struct UserSessionInfo {
    uint16_t userLevel = 0;
    uint32_t userExp = 0;
};

struct UserInfo {
    std::string userId;
    uint32_t exp = 0;
    uint16_t level = 1;
};

struct Currency {
    uint32_t bp = 0;
    uint32_t gcoin = 0;
};

struct InventoryItem {
    uint32_t itemCode = 0;
    uint32_t quantity = 0;
    uint8_t  itemType = 0;  // 1=코스튬, 2=총스킨, 3=재료
};

struct FriendInfo {
    char    friendId[MAX_USER_ID_LEN] = {};
    uint8_t friendStatus = 0;  // 0=요청중, 1=친구
    uint8_t onlineStatus = 0;  // 0=오프라인, 1=로비, 2=게임중 (친구일 때)
};

// DB 조회용 내부 구조체 (헤더에 넣지 말고 cpp 내부에서만)
struct FriendInfoDB {
    uint32_t friendPk = 0;
    char     friendId[MAX_USER_ID_LEN] = {};
    uint8_t  friendStatus = 0;
};

// 친구 찾기용 구조체
struct UserSearchResult {
    char userId[MAX_USER_ID_LEN] = {};
    uint32_t userPk = 0;
    uint16_t userLevel = 0;
};