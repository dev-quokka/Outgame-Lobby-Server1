#pragma once
#include <cstdint>

enum class CostumeChangeFailCode : uint8_t {
    None = 0, 
    NotInInventory = 1,  // 인벤에 없는 아이템
    InvalidSlot = 2,   // 잘못된 슬롯 번호
    ServerError = 3, // 서버 에러
};



enum class PartyFailCode : uint8_t {
    None = 0,
    AlreadyInParty = 1,  // 해당 유저 이미 파티 있음
    PartyFull = 2,      // 파티 꽉참
    UserNotFound = 3,   // 유저 없음
    ServerError = 4, // 서버 에러
    NotInParty = 5, // 현재 파티 없음 (잘못된 요청)


    NotLeader = 11, // 파티 리더 아님
};