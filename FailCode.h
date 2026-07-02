#pragma once
#include <cstdint>

enum class CostumeChangeFailCode : uint8_t {
    None = 0, 
    NotInInventory = 1,  // 인벤에 없는 아이템
    InvalidSlot = 2,   // 잘못된 슬롯 번호
    ServerError = 3, // 서버 에러
};